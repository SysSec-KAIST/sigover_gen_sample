#include "srslte/srslte.h"
#include "srslte/asn1/rrc_asn1.h"
#include "srslte/common/bcd_helpers.h"
#include "srslte/common/log_filter.h"

using namespace asn1;
using namespace asn1::rrc;

uint32_t imsi_to_array(std::string imsi_s, uint8_t* buff) {
  uint32_t len = imsi_s.length();
  unsigned long long imsi = std::stoull(imsi_s);
  for (int i = len-1; i >= 0; i--) {
    buff[i] = imsi%10;
    imsi = imsi/10;
  }
  return len;
}

int gen_paging(uint8_t* buffer, uint32_t buffer_len, uint32_t* msg_len, uint8_t* imsi, uint32_t imsi_len) {
  bit_ref bref(buffer, buffer_len);
  pcch_msg_s pcch_msg;
  pcch_msg.msg.set_c1();
  paging_s& paging = pcch_msg.msg.c1().paging();
  paging.paging_record_list_present = true;

  paging_record_s paging_elem;
  paging_elem.ue_id.set_imsi(); // imsi_l, bounded_array
  memcpy(paging_elem.ue_id.imsi().data(), imsi, imsi_len);
  paging_elem.ue_id.imsi().resize(imsi_len);
  paging_elem.cn_domain = paging_record_s::cn_domain_e_::ps; // CN domain

  paging.paging_record_list.push_back(paging_elem);

  if (pcch_msg.pack(bref) != SRSASN_SUCCESS) {
    return -1;
  }

  int len = bref.distance_bytes(buffer);
  srslte_vec_fprint_byte(stdout, buffer, len);
  *msg_len = len;

  return SRSLTE_SUCCESS;
}

