#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "gen_sample.h"

srslte_cell_t cell = {
    100,               // nof_prb
    2,                 // nof_ports
    123,               // cell_id
    SRSLTE_CP_NORM,    // cyclic prefix
    SRSLTE_PHICH_NORM, // PHICH length
    SRSLTE_PHICH_R_1,  // PHICH resources
    SRSLTE_FDD,
};

srslte_tm_t tm = SRSLTE_TM2;
bool enable_256qam = false;
uint16_t rnti = 0xfffe;
uint32_t cfi = 3;
uint32_t tti = 0;
bool write_file = false;
FILE *fp = NULL;
cf_t zero_buff[10240] = {0,};

srslte_enb_dl_t* enb_dl = NULL;
srslte_softbuffer_tx_t* softbuffer_tx[SRSLTE_MAX_TB] = {};
uint8_t *payload[SRSLTE_MAX_TB] = {};
uint32_t payload_len = 0;
cf_t *signal_buffer[SRSLTE_MAX_PORTS] = {NULL};
srslte_dl_sf_cfg_t sf_cfg_dl = {0,};


void usage(char *prog) {
  printf("\t-c cell id [Default %d]\n", cell.id);
  printf("\t-f cfi [Default %d]\n", cfi);
  printf("\t-p cell.nof_prb [Default %d]\n", cell.nof_prb);
  printf("\t-s tti [Default %d]\n", tti);
  printf("\t-r rnti [Default 0x%x\n", rnti);
  printf("\t-i wrtie file [Default %d]\n", write_file);
}


void parse_args(int argc, char **argv) {
  int opt;

  while ((opt = getopt(argc, argv, "rfspcivw")) != -1) {
    switch (opt) {
      case 'r':
        rnti = (uint16_t)strtol(argv[optind], NULL, 16);
        break;
      case 'f':
        cfi = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 's':
        tti = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'p':
        cell.nof_prb = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'c':
        cell.id = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'i':
        write_file = true;
        break;
      case 'v':
        srslte_verbose += 1;
        break;
      case 'w':
        srslte_verbose += 2;
        break;
      default:
        usage(argv[0]);
        exit(-1);
    }
  }
}

int generate_format1a_broadcast(uint32_t tbs_bytes, uint32_t cell_nof_prb, uint32_t rv, uint16_t rnti, srslte_dci_dl_t* dci)
{
  /* Calculate I_tbs for this TBS */
  uint32_t l_crb = 0;
  uint32_t rb_start = 0;
  int tbs = tbs_bytes * 8;
  int i;
  int mcs = -1;
  for (i = 0; i < 27; i++) {
    if (srslte_ra_tbs_from_idx(i, 2) >= tbs) {
      dci->type2_alloc.n_prb1a = srslte_ra_type2_t::SRSLTE_RA_TYPE2_NPRB1A_2;
      l_crb                    = 2;
      mcs                      = i;
      tbs                      = srslte_ra_tbs_from_idx(i, 2);
      break;
    } else if (srslte_ra_tbs_from_idx(i, 3) >= tbs) {
      dci->type2_alloc.n_prb1a = srslte_ra_type2_t::SRSLTE_RA_TYPE2_NPRB1A_3;
      l_crb                    = 3;
      mcs                      = i;
      tbs                      = srslte_ra_tbs_from_idx(i, 3);
      break;
    }
  }
  if (i == 28) {
    ERROR("Can't allocate Format 1A for TBS=%d\n", tbs);
    return -1;
  }
  if (l_crb == 0) {
    ERROR("L_crb is 0\n");
    return -1;
  }

  INFO("ra_tbs=%d/%d, tbs_bytes=%d, tbs=%d, mcs=%d\n",
        srslte_ra_tbs_from_idx(mcs, 2),
        srslte_ra_tbs_from_idx(mcs, 3),
        tbs_bytes,
        tbs,
        mcs);

  dci->alloc_type       = SRSLTE_RA_ALLOC_TYPE2;
  dci->type2_alloc.mode = srslte_ra_type2_t::SRSLTE_RA_TYPE2_LOC;
  dci->type2_alloc.riv  = srslte_ra_type2_to_riv(l_crb, rb_start, cell_nof_prb);
  dci->pid              = 0;
  dci->tb[0].mcs_idx    = mcs;
  dci->tb[0].rv         = rv;
  dci->format           = SRSLTE_DCI_FORMAT1A;
  dci->rnti             = rnti;

  return tbs;

}

int main(int argc, char** argv) {
  parse_args(argc,argv);

  if (write_file) {
    fp = fopen("output", "wb");
  }

  // init, after acquire cell info.

  enb_dl = (srslte_enb_dl_t* )srslte_vec_malloc(sizeof(srslte_enb_dl_t));
  if (!enb_dl) {
    ERROR("Error allocating buffer\n");
    return -1;
  }

  for (uint32_t i = 0; i < cell.nof_ports; i++) {
    signal_buffer[i] = (cf_t *)srslte_vec_malloc(sizeof(cf_t) * SRSLTE_SF_LEN_MAX);
    if (!signal_buffer[i]) {
      ERROR("Error allocating buffer\n");
      return -1;
    }
  }

  for (int i = 0; i < SRSLTE_MAX_TB; i++) {
    softbuffer_tx[i] = (srslte_softbuffer_tx_t *) calloc(sizeof(srslte_softbuffer_tx_t), 1);
    if (!softbuffer_tx[i]) {
      ERROR("Error allocating softbuffer_tx\n");
      return -1;
    }

    if (srslte_softbuffer_tx_init(softbuffer_tx[i], SRSLTE_MAX_PRB)) {
      ERROR("Error initiating softbuffer_tx\n");
      return -1;
    }

    payload[i] = (uint8_t *)srslte_vec_malloc(sizeof(uint8_t) * 2048);
    if (!payload[i]) {
      ERROR("Error allocating data tx\n");
      return -1;
    }
    memset(payload[i], 0, sizeof(uint8_t) * 2048);
  }
  
  if (srslte_enb_dl_init(enb_dl, signal_buffer, SRSLTE_MAX_PRB)) {
    ERROR("Error initiating eNb downlink\n");
    return -1;
  }

  if (srslte_enb_dl_set_cell(enb_dl, cell)) {
    ERROR("Error setting eNb DL cell\n");
    return -1;
  }
  // init end
  if (rnti != 0xfffe) {
    printf("Paging only\n");
    exit(-1);
  }
  
  //Gen payload
  uint8_t imsi_buff[100] = {0,};
  std::string imsi  = "123456789012345";
  uint32_t imsi_len = imsi_to_array(imsi, imsi_buff);
  
  gen_paging(payload[0], sizeof(uint8_t) * 2048, &payload_len, imsi_buff, imsi_len);

  // sf_cfg_dl
  sf_cfg_dl.tti = tti;
  sf_cfg_dl.cfi = cfi;
  sf_cfg_dl.sf_type = srslte_sf_t::SRSLTE_SF_NORM;


  // dynamic

  // generate dci
  srslte_dci_location_t dci_locations[MAX_CANDIDATES];
  uint32_t num_locations;
  num_locations = srslte_pdcch_common_locations(&enb_dl->pdcch, dci_locations, MAX_CANDIDATES, cfi);

  srslte_dci_dl_t dci = {0,};
  srslte_dci_cfg_t dci_cfg = {0,};

  dci.location = dci_locations[0];
  generate_format1a_broadcast(payload_len, cell.nof_prb, 0, rnti, &dci);

  // encode
  srslte_enb_dl_put_base_wo_sync(enb_dl, &sf_cfg_dl);
  if (srslte_enb_dl_put_pdcch_dl(enb_dl, &dci_cfg, &dci)) {
    ERROR("Error putting PDCCH sf_idx=%d\n", sf_cfg_dl.tti);
    return -1;
  }

  srslte_pdsch_cfg_t pdsch_cfg;
  if (srslte_ra_dl_dci_to_grant(&cell, &sf_cfg_dl, tm, enable_256qam, &dci, &pdsch_cfg.grant)) {
    ERROR("Computing DL grant sf_idx=%d\n", sf_cfg_dl.tti);
    return -1;
  }

  char str[512];
  srslte_dci_dl_info(&dci, str, 512);
  INFO("eNb PDCCH: rnti=0x%x, %s\n", rnti, str);
  
  for (uint32_t i = 0; i < SRSLTE_MAX_CODEWORDS; i++) {
    pdsch_cfg.softbuffers.tx[i] = softbuffer_tx[i];
  }

  // Enable power allocation
  pdsch_cfg.power_scale  = true;
  pdsch_cfg.p_a          = 0.0f;                      // 0 dB
  pdsch_cfg.p_b          = (tm > SRSLTE_TM1) ? 1 : 0; // 0 dB
  pdsch_cfg.rnti         = rnti;
  pdsch_cfg.meas_time_en = false;

  if (srslte_enb_dl_put_pdsch(enb_dl, &pdsch_cfg, payload) < 0) {
    ERROR("Error putting PDSCH sf_idx=%d\n", sf_cfg_dl.tti);
  }
  srslte_pdsch_tx_info(&pdsch_cfg, str, 512);
  INFO("eNb PDSCH: rnti=0x%x, %s\n", rnti, str);

  srslte_enb_dl_gen_signal(enb_dl);

  //write to file
  //signal_buffer
  uint32_t zero_padding_len = SRSLTE_SF_LEN_PRB(cell.nof_prb)/10;
  printf("padding: %d\n",zero_padding_len);
  printf("%d\n",SRSLTE_SF_LEN_PRB(cell.nof_prb));
  printf("%lu\n", sizeof(_Complex float));

  if (write_file) {
    fwrite(zero_buff, zero_padding_len * sizeof(cf_t), 1, fp); // pre padding
    fwrite(signal_buffer[0], SRSLTE_SF_LEN_PRB(cell.nof_prb) * sizeof(cf_t), 1, fp);
    fwrite(zero_buff, zero_padding_len * sizeof(cf_t), 1, fp); // post padding
    fclose(fp);
  }

  return 0;
}
