/*
 * @Description: STSW-ST25RFAL002 ST25R3916 NFC discovery example
 * @Author: LILYGO_L
 * @Date: 2026-07-17 21:35:06
 * @LastEditTime: 2026-07-20 15:45:00
 * @License: GPL 3.0
 */
#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "common.h"
#include "stsw_st25rfal002_cpp_bus_driver_library.h"

namespace stsw = stsw_st25rfal002_cpp_bus_driver;

namespace {

constexpr char kSectionLine[] =
    "================================================================";
constexpr uint32_t kCardRemovalTimeoutMs = 3000;
constexpr size_t kTrackedNfcIdCapacity = 16;

struct CardFingerprint {
  rfalNfcDevType type = RFAL_NFC_LISTEN_TYPE_PROP;
  uint8_t nfcid[kTrackedNfcIdCapacity] = {};
  uint8_t nfcid_length = 0;
  bool valid = false;
};

const char* RfalErrorName(ReturnCode result);
void ReadAndPrintType2TagContent();

/**
 * @brief 将布尔状态转换为便于阅读的文本
 * @param value 需要转换的布尔值
 * @return true 返回 Yes，false 返回 No
 */
const char* YesNo(bool value) { return value ? "Yes" : "No"; }

/**
 * @brief 获取 NFC 设备类型名称
 * @param type RFAL NFC 设备类型
 * @return 人类可读的设备类型名称
 */
const char* DeviceTypeName(rfalNfcDevType type) {
  switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      return "NFC-A card / tag";
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      return "NFC-B card / tag";
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      return "NFC-F card / tag (FeliCa)";
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      return "NFC-V card / tag";
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      return "ST25TB card / tag";
    case RFAL_NFC_LISTEN_TYPE_AP2P:
      return "NFC peer-to-peer target";
    case RFAL_NFC_LISTEN_TYPE_PROP:
      return "Proprietary NFC device";
    case RFAL_NFC_POLL_TYPE_NFCA:
      return "NFC-A reader / poller";
    case RFAL_NFC_POLL_TYPE_NFCB:
      return "NFC-B reader / poller";
    case RFAL_NFC_POLL_TYPE_NFCF:
      return "NFC-F reader / poller";
    case RFAL_NFC_POLL_TYPE_NFCV:
      return "NFC-V reader / poller";
    case RFAL_NFC_POLL_TYPE_AP2P:
      return "NFC peer-to-peer initiator";
    default:
      return "Unknown NFC device";
  }
}

/**
 * @brief 获取 NFC 技术标准名称
 * @param type RFAL NFC 设备类型
 * @return 人类可读的标准名称
 */
const char* TechnologyName(rfalNfcDevType type) {
  switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
    case RFAL_NFC_POLL_TYPE_NFCA:
      return "NFC-A / ISO/IEC 14443 Type A";
    case RFAL_NFC_LISTEN_TYPE_NFCB:
    case RFAL_NFC_POLL_TYPE_NFCB:
      return "NFC-B / ISO/IEC 14443 Type B";
    case RFAL_NFC_LISTEN_TYPE_NFCF:
    case RFAL_NFC_POLL_TYPE_NFCF:
      return "NFC-F / JIS X 6319-4 (FeliCa)";
    case RFAL_NFC_LISTEN_TYPE_NFCV:
    case RFAL_NFC_POLL_TYPE_NFCV:
      return "NFC-V / ISO/IEC 15693";
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      return "ST25TB proprietary vicinity tag";
    case RFAL_NFC_LISTEN_TYPE_AP2P:
    case RFAL_NFC_POLL_TYPE_AP2P:
      return "NFC-DEP / ISO/IEC 18092 peer-to-peer";
    default:
      return "Technology not identified";
  }
}

/**
 * @brief 获取已激活 RF 接口的名称
 * @param interface_type RFAL RF 接口
 * @return 人类可读的接口名称
 */
const char* InterfaceName(rfalNfcRfInterface interface_type) {
  switch (interface_type) {
    case RFAL_NFC_INTERFACE_RF:
      return "Technology-specific RF frames";
    case RFAL_NFC_INTERFACE_ISODEP:
      return "ISO-DEP / ISO/IEC 14443-4";
    case RFAL_NFC_INTERFACE_NFCDEP:
      return "NFC-DEP peer-to-peer protocol";
    default:
      return "Unknown RF interface";
  }
}

/**
 * @brief 获取 RFAL 速率的可读名称
 * @param bit_rate RFAL 位速率
 * @return 带单位的速率名称
 */
const char* BitRateName(rfalBitRate bit_rate) {
  switch (bit_rate) {
    case RFAL_BR_106:
      return "106 kbit/s";
    case RFAL_BR_212:
      return "212 kbit/s";
    case RFAL_BR_424:
      return "424 kbit/s";
    case RFAL_BR_848:
      return "848 kbit/s";
    case RFAL_BR_1695:
      return "1695 kbit/s";
    case RFAL_BR_3390:
      return "3390 kbit/s";
    case RFAL_BR_6780:
      return "6780 kbit/s";
    case RFAL_BR_13560:
      return "13560 kbit/s";
    case RFAL_BR_211p88:
      return "211.88 kbit/s";
    case RFAL_BR_105p94:
      return "105.94 kbit/s";
    case RFAL_BR_52p97:
      return "52.97 kbit/s";
    case RFAL_BR_26p48:
      return "26.48 kbit/s";
    case RFAL_BR_1p66:
      return "1.66 kbit/s";
    case RFAL_BR_KEEP:
      return "Unchanged";
    default:
      return "Unknown bit rate";
  }
}

/**
 * @brief 获取 NFC-A 卡片配置名称
 * @param type NFC-A 监听设备类型
 * @return NFC Forum 卡片配置名称
 */
const char* NfcaProfileName(rfalNfcaListenDeviceType type) {
  switch (type) {
    case RFAL_NFCA_T1T:
      return "NFC Forum Type 1 Tag (T1T)";
    case RFAL_NFCA_T2T:
      return "NFC Forum Type 2 Tag (T2T)";
    case RFAL_NFCA_T4T:
      return "NFC Forum Type 4 Tag (T4T / ISO-DEP)";
    case RFAL_NFCA_NFCDEP:
      return "NFC-DEP peer-to-peer device";
    case RFAL_NFCA_T4T_NFCDEP:
      return "Type 4 Tag and NFC-DEP multi-protocol device";
    default:
      return "Unknown NFC-A profile";
  }
}

/**
 * @brief 获取 ISO/IEC 14443 应用族名称
 * @param afi 应用族标识符
 * @return 应用族的可读名称
 */
const char* AfiFamilyName(uint8_t afi) {
  switch (afi >> 4U) {
    case 0x0:
      return "All application families";
    case 0x1:
      return "Transport";
    case 0x2:
      return "Financial";
    case 0x3:
      return "Identification";
    case 0x4:
      return "Telecommunication";
    case 0x5:
      return "Medical";
    case 0x6:
      return "Multimedia";
    case 0x7:
      return "Gaming";
    case 0x8:
      return "Data storage";
    case 0x9:
      return "Item management";
    case 0xA:
      return "Express parcels";
    case 0xB:
      return "Postal services";
    case 0xC:
      return "Airline bags";
    default:
      return "Reserved application family";
  }
}

/**
 * @brief 获取常见 ISO 芯片制造商名称
 * @param manufacturer_code ISO/IEC 7816-6 制造商代码
 * @return 制造商名称，未收录时返回 Unknown manufacturer
 */
const char* ManufacturerName(uint8_t manufacturer_code) {
  switch (manufacturer_code) {
    case 0x02:
      return "STMicroelectronics";
    case 0x04:
      return "NXP Semiconductors";
    case 0x05:
      return "Infineon Technologies";
    case 0x07:
      return "Texas Instruments";
    default:
      return "Unknown manufacturer";
  }
}

/**
 * @brief 将 FSCI 转换为最大帧长度
 * @param fsci 帧大小整数编码
 * @return 最大帧长度，编码无效时返回 0
 */
uint16_t FrameSizeFromFsci(uint8_t fsci) {
  constexpr uint16_t kFrameSizes[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
  return fsci < sizeof(kFrameSizes) / sizeof(kFrameSizes[0])
             ? kFrameSizes[fsci]
             : 0;
}

/**
 * @brief 按统一格式打印字段名称
 * @param label 字段名称
 */
void PrintFieldLabel(const char* label) { std::printf("  %-28s : ", label); }

/**
 * @brief 将字节序列打印成带冒号分隔的十六进制标识符
 * @param label 字段名称
 * @param data 字节序列
 * @param length 字节数量
 * @param reverse 是否反向显示字节顺序
 */
void PrintHexField(
    const char* label, const uint8_t* data, size_t length, bool reverse = false) {
  PrintFieldLabel(label);
  if (data == nullptr || length == 0) {
    std::printf("Not available\n");
    return;
  }
  for (size_t index = 0; index < length; ++index) {
    const size_t data_index = reverse ? length - 1U - index : index;
    std::printf("%02X%s", data[data_index], index + 1U < length ? ":" : "");
  }
  std::printf("\n");
}

/**
 * @brief 输出当前 RF 收发速率
 */
void PrintActiveBitRates() {
  rfalBitRate transmit_rate = RFAL_BR_KEEP;
  rfalBitRate receive_rate = RFAL_BR_KEEP;
  if (rfalGetBitRate(&transmit_rate, &receive_rate) == RFAL_ERR_NONE) {
    PrintFieldLabel("Reader transmit rate");
    std::printf("%s\n", BitRateName(transmit_rate));
    PrintFieldLabel("Reader receive rate");
    std::printf("%s\n", BitRateName(receive_rate));
  }
}

/**
 * @brief 输出 NFC-A 特有信息
 * @param card NFC-A 卡片发现信息
 */
void PrintNfcaDetails(const rfalNfcaListenDevice& card) {
  const uint16_t atqa = static_cast<uint16_t>(card.sensRes.platformInfo) << 8U |
                        card.sensRes.anticollisionInfo;
  PrintFieldLabel("Card profile");
  std::printf("%s\n", NfcaProfileName(card.type));
  PrintHexField("UID / NFCID1", card.nfcId1, card.nfcId1Len);
  PrintFieldLabel("UID length");
  std::printf("%u bytes (%u cascade level%s)\n",
      static_cast<unsigned int>(card.nfcId1Len),
      static_cast<unsigned int>(card.nfcId1Len <= 4 ? 1 :
              (card.nfcId1Len <= 7 ? 2 : 3)),
      card.nfcId1Len <= 4 ? "" : "s");
  PrintFieldLabel("ATQA / SENS_RES");
  std::printf("0x%04X\n", static_cast<unsigned int>(atqa));
  PrintFieldLabel("SAK / SEL_RES");
  std::printf("0x%02X\n", static_cast<unsigned int>(card.selRes.sak));
  PrintFieldLabel("ISO-DEP capable");
  std::printf("%s\n", YesNo(card.type == RFAL_NFCA_T4T ||
                            card.type == RFAL_NFCA_T4T_NFCDEP));
  PrintFieldLabel("NFC-DEP capable");
  std::printf("%s\n", YesNo(card.type == RFAL_NFCA_NFCDEP ||
                            card.type == RFAL_NFCA_T4T_NFCDEP));
  PrintFieldLabel("Sleep state");
  std::printf("%s\n", card.isSleep ? "Sleeping" : "Awake / selected");
}

/**
 * @brief 输出 NFC-B 支持的通信速率
 * @param capability SENSB_RES 位速率能力字节
 */
void PrintNfcbRateCapabilities(uint8_t capability) {
  PrintFieldLabel("Reader to card rates");
  std::printf("106");
  if ((capability & 0x01U) != 0U) {
    std::printf(", 212");
  }
  if ((capability & 0x02U) != 0U) {
    std::printf(", 424");
  }
  if ((capability & 0x04U) != 0U) {
    std::printf(", 848");
  }
  std::printf(" kbit/s\n");
  PrintFieldLabel("Card to reader rates");
  std::printf("106");
  if ((capability & 0x10U) != 0U) {
    std::printf(", 212");
  }
  if ((capability & 0x20U) != 0U) {
    std::printf(", 424");
  }
  if ((capability & 0x40U) != 0U) {
    std::printf(", 848");
  }
  std::printf(" kbit/s\n");
  PrintFieldLabel("Same rate required");
  std::printf("%s\n", YesNo((capability & 0x80U) != 0U));
}

/**
 * @brief 输出 NFC-B 特有信息
 * @param card NFC-B 卡片发现信息
 */
void PrintNfcbDetails(const rfalNfcbListenDevice& card) {
  const auto& response = card.sensbRes;
  const uint8_t fsci = rfalNfcbGetFSCI(&response);
  const uint8_t fwi = response.protInfo.FwiAdcFo >> 4U;
  PrintFieldLabel("Card profile");
  std::printf("%s\n", rfalNfcbIsIsoDepSupported(&card)
                            ? "NFC Forum Type 4 Tag (T4T / ISO-DEP)"
                            : "NFC-B RF card");
  PrintHexField("PUPI / NFCID0", response.nfcid0, RFAL_NFCB_NFCID0_LEN);
  PrintFieldLabel("ATQB / SENSB_RES length");
  std::printf("%u bytes\n", static_cast<unsigned int>(card.sensbResLen));
  PrintFieldLabel("Application family (AFI)");
  std::printf("0x%02X - %s\n", static_cast<unsigned int>(response.appData.AFI),
      AfiFamilyName(response.appData.AFI));
  PrintFieldLabel("Applications reported");
  std::printf("%u\n", static_cast<unsigned int>(response.appData.numApps));
  PrintHexField("Application data CRC_B", response.appData.CRC_B,
      RFAL_NFCB_CRC_LEN);
  PrintNfcbRateCapabilities(response.protInfo.BRC);
  PrintFieldLabel("Maximum frame size");
  const uint16_t frame_size = FrameSizeFromFsci(fsci);
  if (frame_size == 0) {
    std::printf("Unknown (FSCI=%u)\n", static_cast<unsigned int>(fsci));
  } else {
    std::printf("%u bytes (FSCI=%u)\n", static_cast<unsigned int>(frame_size),
        static_cast<unsigned int>(fsci));
  }
  PrintFieldLabel("Frame waiting integer");
  std::printf("FWI=%u\n", static_cast<unsigned int>(fwi));
  PrintFieldLabel("Advanced protocol features");
  std::printf("%s\n",
      YesNo((response.protInfo.FwiAdcFo &
                RFAL_NFCB_SENSB_RES_ADC_ADV_FEATURE_MASK) != 0U));
  PrintFieldLabel("Proprietary application");
  std::printf("%s\n",
      YesNo((response.protInfo.FwiAdcFo &
                RFAL_NFCB_SENSB_RES_ADC_PROPRIETARY_MASK) != 0U));
  PrintFieldLabel("DID addressing supported");
  std::printf("%s\n", YesNo((response.protInfo.FwiAdcFo &
                                  RFAL_NFCB_SENSB_RES_FO_DID_MASK) != 0U));
  PrintFieldLabel("NAD addressing supported");
  std::printf("%s\n", YesNo((response.protInfo.FwiAdcFo &
                                  RFAL_NFCB_SENSB_RES_FO_NAD_MASK) != 0U));
  if (card.sensbResLen >= RFAL_NFCB_SENSB_RES_EXT_LEN) {
    PrintFieldLabel("Start-up guard integer");
    std::printf("SFGI=%u\n",
        static_cast<unsigned int>(response.protInfo.SFGI >> 4U));
  }
  PrintFieldLabel("Sleep state");
  std::printf("%s\n", card.isSleep ? "Sleeping" : "Awake / selected");
}

/**
 * @brief 输出 NFC-F 特有信息
 * @param card NFC-F 卡片发现信息
 */
void PrintNfcfDetails(const rfalNfcfListenDevice& card) {
  const auto& response = card.sensfRes;
  uint8_t manufacturer_parameter[8] = {response.PAD0[0], response.PAD0[1],
      response.PAD1[0], response.PAD1[1], response.PAD1[2],
      response.MRTIcheck, response.MRTIupdate, response.PAD2};
  PrintFieldLabel("Card profile");
  std::printf("%s\n", rfalNfcfIsNfcDepSupported(&card)
                            ? "NFC-DEP peer-to-peer target"
                            : "NFC Forum Type 3 Tag (T3T / FeliCa)");
  PrintHexField("IDm / NFCID2", response.NFCID2, RFAL_NFCF_NFCID2_LEN);
  PrintFieldLabel("SENSF_RES length");
  std::printf("%u bytes\n", static_cast<unsigned int>(card.sensfResLen));
  PrintHexField(
      "Manufacturer parameter / PMm", manufacturer_parameter,
      sizeof(manufacturer_parameter));
  PrintFieldLabel("Response time: check");
  std::printf("MRTI=0x%02X\n", static_cast<unsigned int>(response.MRTIcheck));
  PrintFieldLabel("Response time: update");
  std::printf("MRTI=0x%02X\n", static_cast<unsigned int>(response.MRTIupdate));
  PrintHexField("Request data / system code", response.RD,
      RFAL_NFCF_SENSF_RES_RD_LEN);
  PrintFieldLabel("NFC-DEP capable");
  std::printf("%s\n", YesNo(rfalNfcfIsNfcDepSupported(&card)));
}

/**
 * @brief 输出 NFC-V 特有信息
 * @param card NFC-V 卡片发现信息
 */
void PrintNfcvDetails(const rfalNfcvListenDevice& card) {
  const auto& response = card.InvRes;
  PrintFieldLabel("Card profile");
  std::printf("NFC Forum Type 5 Tag (T5T)\n");
  PrintHexField("UID (canonical order)", response.UID, RFAL_NFCV_UID_LEN, true);
  if (response.UID[RFAL_NFCV_UID_LEN - 1U] == 0xE0U) {
    const uint8_t manufacturer = response.UID[RFAL_NFCV_UID_LEN - 2U];
    PrintFieldLabel("IC manufacturer");
    std::printf("%s (code 0x%02X)\n", ManufacturerName(manufacturer),
        static_cast<unsigned int>(manufacturer));
  }
  PrintFieldLabel("Inventory response");
  std::printf("%s%s\n",
      (response.RES_FLAG & RFAL_NFCV_RES_FLAG_ERROR) == 0U ? "Valid" : "Error",
      (response.RES_FLAG & RFAL_NFCV_RES_FLAG_EXTENSION) != 0U
          ? ", protocol extension present"
          : "");
  PrintFieldLabel("Data format identifier");
  std::printf("0x%02X%s\n", static_cast<unsigned int>(response.DSFID),
      response.DSFID == 0 ? " (not specified)" : "");
  PrintFieldLabel("Sleep state");
  std::printf("%s\n", card.isSleep ? "Sleeping" : "Awake / selected");
}

/**
 * @brief 输出 ST25TB 特有信息
 * @param card ST25TB 卡片发现信息
 */
void PrintSt25tbDetails(const rfalSt25tbListenDevice& card) {
  PrintFieldLabel("Card profile");
  std::printf("ST25TB proprietary memory tag\n");
  PrintHexField("UID", card.UID, RFAL_ST25TB_UID_LEN);
  PrintFieldLabel("Session chip ID");
  std::printf("0x%02X\n", static_cast<unsigned int>(card.chipID));
  PrintFieldLabel("Selection state");
  std::printf("%s\n", card.isDeselected ? "Deselected" : "Selected");
}

/**
 * @brief 输出 ISO-DEP 协议协商信息
 * @param device 已激活的 NFC 设备
 */
void PrintIsoDepDetails(const rfalNfcDevice& device) {
  const auto& iso_dep = device.proto.isoDep;
  const auto& info = iso_dep.info;
  const uint64_t frame_wait_us =
      (static_cast<uint64_t>(info.FWT) * 1000000ULL + 6780000ULL) /
      13560000ULL;
  std::printf("\n[ ISO-DEP protocol ]\n");
  PrintFieldLabel("Maximum protocol frame");
  std::printf("%u bytes (FSxI=%u)\n", static_cast<unsigned int>(info.FSx),
      static_cast<unsigned int>(info.FSxI));
  PrintFieldLabel("Card to reader rate");
  std::printf("%s\n", BitRateName(info.DSI));
  PrintFieldLabel("Reader to card rate");
  std::printf("%s\n", BitRateName(info.DRI));
  PrintFieldLabel("Frame waiting time");
  std::printf("%" PRIu64 " us (FWI=%u)\n", frame_wait_us,
      static_cast<unsigned int>(info.FWI));
  PrintFieldLabel("Start-up guard time");
  std::printf("%" PRIu32 " ms (SFGI=%" PRIu32 ")\n", info.SFGT, info.SFGI);
  PrintFieldLabel("DID supported / active");
  std::printf("%s / %u\n", YesNo(info.supDID),
      static_cast<unsigned int>(info.DID));
  PrintFieldLabel("NAD supported / active");
  std::printf("%s / %u\n", YesNo(info.supNAD),
      static_cast<unsigned int>(info.NAD));
  PrintFieldLabel("Advanced features");
  std::printf("%s\n", YesNo(info.supAdFt));

  if (device.type == RFAL_NFC_LISTEN_TYPE_NFCA) {
    const auto& ats = iso_dep.activation.A.Listener.ATS;
    PrintFieldLabel("ATS length");
    std::printf("%u bytes\n", static_cast<unsigned int>(
        iso_dep.activation.A.Listener.ATSLen));
    PrintFieldLabel("ATS format byte T0");
    std::printf("0x%02X\n", static_cast<unsigned int>(ats.T0));
    const uint8_t interface_bytes = static_cast<uint8_t>(
        ((ats.T0 & 0x10U) != 0U ? 1U : 0U) +
        ((ats.T0 & 0x20U) != 0U ? 1U : 0U) +
        ((ats.T0 & 0x40U) != 0U ? 1U : 0U));
    const uint8_t historical_length =
        ats.TL > static_cast<uint8_t>(2U + interface_bytes)
            ? static_cast<uint8_t>(ats.TL - 2U - interface_bytes)
            : 0U;
    PrintHexField("ATS historical bytes", ats.HB,
        std::min<size_t>(historical_length, RFAL_ISODEP_ATS_HB_MAX_LEN));
  } else if (device.type == RFAL_NFC_LISTEN_TYPE_NFCB) {
    const auto& attrib = iso_dep.activation.B.Listener;
    const size_t higher_layer_length = attrib.ATTRIB_RESLen > 1U
                                           ? attrib.ATTRIB_RESLen - 1U
                                           : 0U;
    PrintFieldLabel("ATTRIB response length");
    std::printf("%u bytes\n",
        static_cast<unsigned int>(attrib.ATTRIB_RESLen));
    PrintHexField("Higher-layer response", attrib.ATTRIB_RES.HLInfo,
        std::min<size_t>(higher_layer_length, RFAL_ISODEP_ATTRIB_HLINFO_LEN));
  }
}

/**
 * @brief 输出 NFC-DEP 协议协商信息
 * @param device 已激活的 NFC 设备
 */
void PrintNfcDepDetails(const rfalNfcDevice& device) {
  const auto& nfc_dep = device.proto.nfcDep;
  const auto& info = nfc_dep.info;
  std::printf("\n[ NFC-DEP protocol ]\n");
  PrintFieldLabel("Maximum payload frame");
  std::printf("%u bytes (LR=%u)\n", static_cast<unsigned int>(info.FS),
      static_cast<unsigned int>(info.LR));
  PrintFieldLabel("Initiator to target rate");
  std::printf("%s\n", BitRateName(info.DSI));
  PrintFieldLabel("Target to initiator rate");
  std::printf("%s\n", BitRateName(info.DRI));
  PrintFieldLabel("Device ID / node address");
  std::printf("%u / %u\n", static_cast<unsigned int>(info.DID),
      static_cast<unsigned int>(info.NAD));
  PrintFieldLabel("General bytes length");
  std::printf("%u bytes\n", static_cast<unsigned int>(info.GBLen));
  if (device.type == RFAL_NFC_LISTEN_TYPE_AP2P ||
      device.type == RFAL_NFC_LISTEN_TYPE_NFCA ||
      device.type == RFAL_NFC_LISTEN_TYPE_NFCF) {
    PrintHexField("General bytes",
        nfc_dep.activation.Target.ATR_RES.GBt,
        std::min<size_t>(info.GBLen, RFAL_NFCDEP_GB_MAX_LEN));
  } else {
    PrintHexField("General bytes",
        nfc_dep.activation.Initiator.ATR_REQ.GBi,
        std::min<size_t>(info.GBLen, RFAL_NFCDEP_GB_MAX_LEN));
  }
}

/**
 * @brief 输出一张 NFC 卡片的全部发现和协议协商信息
 * @param device 已激活的 NFC 设备
 * @param report_number 本次检测序号
 */
void PrintDevice(const rfalNfcDevice& device, uint32_t report_number) {
  std::printf("\n%s\n", kSectionLine);
  std::printf("                    NFC CARD INFORMATION #%-3" PRIu32 "\n",
      report_number);
  std::printf("%s\n", kSectionLine);
  std::printf("[ General information ]\n");
  PrintFieldLabel("Detected device");
  std::printf("%s\n", DeviceTypeName(device.type));
  PrintFieldLabel("Technology / standard");
  std::printf("%s\n", TechnologyName(device.type));
  PrintFieldLabel("Active RF interface");
  std::printf("%s\n", InterfaceName(device.rfInterface));

  const bool reverse_id = device.type == RFAL_NFC_LISTEN_TYPE_NFCV;
  PrintHexField("Primary NFC identifier", device.nfcid, device.nfcidLen,
      reverse_id);
  PrintFieldLabel("Identifier length");
  std::printf("%u bytes\n", static_cast<unsigned int>(device.nfcidLen));
  PrintActiveBitRates();

  std::printf("\n[ Technology details ]\n");
  switch (device.type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      PrintNfcaDetails(device.dev.nfca);
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      PrintNfcbDetails(device.dev.nfcb);
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      PrintNfcfDetails(device.dev.nfcf);
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      PrintNfcvDetails(device.dev.nfcv);
      break;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      PrintSt25tbDetails(device.dev.st25tb);
      break;
    default:
      PrintFieldLabel("Technology details");
      std::printf("No additional technology-specific fields available\n");
      break;
  }

  if (device.rfInterface == RFAL_NFC_INTERFACE_ISODEP) {
    PrintIsoDepDetails(device);
  } else if (device.rfInterface == RFAL_NFC_INTERFACE_NFCDEP) {
    PrintNfcDepDetails(device);
  }
  if (device.type == RFAL_NFC_LISTEN_TYPE_NFCA &&
      device.dev.nfca.type == RFAL_NFCA_T2T) {
    ReadAndPrintType2TagContent();
  }
  std::printf("%s\n", kSectionLine);
  std::printf("Card information complete. Remove the card to scan it again.\n");
  std::printf("%s\n\n", kSectionLine);
}

/**
 * @brief 获取 RFAL 返回码的可读说明
 * @param result RFAL 返回码
 * @return 返回码对应的英文说明
 */
const char* RfalErrorName(ReturnCode result) {
  switch (result) {
    case RFAL_ERR_NONE:
      return "Success";
    case RFAL_ERR_NOMEM:
      return "Not enough memory";
    case RFAL_ERR_BUSY:
      return "Device or resource is busy";
    case RFAL_ERR_IO:
      return "Input/output error";
    case RFAL_ERR_TIMEOUT:
      return "Communication timed out";
    case RFAL_ERR_REQUEST:
      return "Request cannot be executed now";
    case RFAL_ERR_NOMSG:
      return "Expected message was not received";
    case RFAL_ERR_PARAM:
      return "Invalid parameter";
    case RFAL_ERR_SYSTEM:
      return "System error";
    case RFAL_ERR_FRAMING:
      return "RF frame format error";
    case RFAL_ERR_OVERRUN:
      return "Receive buffer overrun";
    case RFAL_ERR_PROTO:
      return "NFC protocol error";
    case RFAL_ERR_INTERNAL:
      return "Internal error";
    case RFAL_ERR_AGAIN:
      return "Operation must be called again";
    case RFAL_ERR_NOT_IMPLEMENTED:
      return "Operation is not implemented";
    case RFAL_ERR_CRC:
      return "CRC check failed";
    case RFAL_ERR_NOTFOUND:
      return "NFC card was not found";
    case RFAL_ERR_NOTUNIQUE:
      return "More than one card responded";
    case RFAL_ERR_NOTSUPP:
      return "Operation is not supported";
    case RFAL_ERR_WRITE:
      return "Write operation failed";
    case RFAL_ERR_FIFO:
      return "RF FIFO overrun or underrun";
    case RFAL_ERR_PAR:
      return "Parity check failed";
    case RFAL_ERR_RF_COLLISION:
      return "RF collision detected";
    case RFAL_ERR_WRONG_STATE:
      return "RFAL is in the wrong state";
    case RFAL_ERR_DISABLED:
      return "Feature is disabled";
    case RFAL_ERR_HW_MISMATCH:
      return "Unexpected NFC hardware";
    case RFAL_ERR_LINK_LOSS:
      return "NFC link was lost";
    default:
      return "Unclassified RFAL error";
  }
}

constexpr uint8_t kType2NdefMagic = 0xE1;
constexpr uint8_t kType2NullTlv = 0x00;
constexpr uint8_t kType2LockControlTlv = 0x01;
constexpr uint8_t kType2MemoryControlTlv = 0x02;
constexpr uint8_t kType2NdefMessageTlv = 0x03;
constexpr uint8_t kType2ProprietaryTlv = 0xFD;
constexpr uint8_t kType2TerminatorTlv = 0xFE;
constexpr size_t kType2HeaderSize = 16;

/**
 * @brief 判断字节序列是否等于指定 ASCII 文本
 * @param data 字节序列
 * @param length 字节数量
 * @param text 以空字符结尾的 ASCII 文本
 * @return 内容完全相同返回 true，否则返回 false
 */
bool BytesEqualText(const uint8_t* data, size_t length, const char* text) {
  return data != nullptr && text != nullptr && std::strlen(text) == length &&
         std::memcmp(data, text, length) == 0;
}

/**
 * @brief 判断负载是否适合直接作为文本显示
 * @param data 负载数据
 * @param length 负载长度
 * @return 不包含空字符和不可显示控制字符返回 true
 */
bool LooksLikeText(const uint8_t* data, size_t length) {
  if (data == nullptr) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const uint8_t value = data[index];
    if (value == 0U || (value < 0x20U && value != '\r' && value != '\n' &&
                          value != '\t') ||
        value == 0x7FU) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 将 UTF-8 或 ASCII 数据转义成单行可读文本
 * @param data 文本数据
 * @param length 文本长度
 */
void PrintEscapedText(const uint8_t* data, size_t length) {
  std::printf("\"");
  if (data != nullptr) {
    for (size_t index = 0; index < length; ++index) {
      const uint8_t value = data[index];
      switch (value) {
        case '\r':
          std::printf("\\r");
          break;
        case '\n':
          std::printf("\\n");
          break;
        case '\t':
          std::printf("\\t");
          break;
        case '\\':
          std::printf("\\\\");
          break;
        case '"':
          std::printf("\\\"");
          break;
        default:
          if (value >= 0x20U && value != 0x7FU) {
            std::printf("%c", static_cast<int>(value));
          } else {
            std::printf(".");
          }
          break;
      }
    }
  }
  std::printf("\"");
}

/**
 * @brief 将 Unicode 码点编码为 UTF-8 并输出
 * @param code_point Unicode 码点
 */
void PrintUnicodeCodePoint(uint32_t code_point) {
  if (code_point == '\r') {
    std::printf("\\r");
  } else if (code_point == '\n') {
    std::printf("\\n");
  } else if (code_point == '\t') {
    std::printf("\\t");
  } else if (code_point == '\\') {
    std::printf("\\\\");
  } else if (code_point == '"') {
    std::printf("\\\"");
  } else if (code_point < 0x20U || code_point == 0x7FU ||
             code_point > 0x10FFFFU) {
    std::printf(".");
  } else if (code_point <= 0x7FU) {
    std::printf("%c", static_cast<int>(code_point));
  } else if (code_point <= 0x7FFU) {
    std::printf("%c%c", static_cast<int>(0xC0U | (code_point >> 6U)),
        static_cast<int>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    std::printf("%c%c%c", static_cast<int>(0xE0U | (code_point >> 12U)),
        static_cast<int>(0x80U | ((code_point >> 6U) & 0x3FU)),
        static_cast<int>(0x80U | (code_point & 0x3FU)));
  } else {
    std::printf("%c%c%c%c", static_cast<int>(0xF0U | (code_point >> 18U)),
        static_cast<int>(0x80U | ((code_point >> 12U) & 0x3FU)),
        static_cast<int>(0x80U | ((code_point >> 6U) & 0x3FU)),
        static_cast<int>(0x80U | (code_point & 0x3FU)));
  }
}

/**
 * @brief 将 NDEF UTF-16 文本转换成 UTF-8 日志
 * @param data UTF-16 文本数据
 * @param length 数据字节数
 */
void PrintUtf16Text(const uint8_t* data, size_t length) {
  bool little_endian = false;
  size_t offset = 0;
  if (length >= 2U && data[0] == 0xFEU && data[1] == 0xFFU) {
    offset = 2U;
  } else if (length >= 2U && data[0] == 0xFFU && data[1] == 0xFEU) {
    little_endian = true;
    offset = 2U;
  }

  std::printf("\"");
  while (offset + 1U < length) {
    uint16_t first = little_endian
                         ? static_cast<uint16_t>(data[offset] |
                               (static_cast<uint16_t>(data[offset + 1U]) << 8U))
                         : static_cast<uint16_t>(
                               (static_cast<uint16_t>(data[offset]) << 8U) |
                               data[offset + 1U]);
    offset += 2U;
    uint32_t code_point = first;
    if (first >= 0xD800U && first <= 0xDBFFU && offset + 1U < length) {
      const uint16_t second = little_endian
                                  ? static_cast<uint16_t>(data[offset] |
                                        (static_cast<uint16_t>(
                                             data[offset + 1U])
                                            << 8U))
                                  : static_cast<uint16_t>(
                                        (static_cast<uint16_t>(data[offset])
                                            << 8U) |
                                        data[offset + 1U]);
      if (second >= 0xDC00U && second <= 0xDFFFU) {
        code_point = 0x10000U +
                     ((static_cast<uint32_t>(first) - 0xD800U) << 10U) +
                     (static_cast<uint32_t>(second) - 0xDC00U);
        offset += 2U;
      }
    }
    PrintUnicodeCodePoint(code_point);
  }
  std::printf("\"");
}

/**
 * @brief 获取 NDEF TNF 类型名称
 * @param tnf NDEF Type Name Format
 * @return TNF 的可读名称
 */
const char* NdefTnfName(uint8_t tnf) {
  switch (tnf) {
    case 0x00:
      return "Empty";
    case 0x01:
      return "NFC Forum well-known type";
    case 0x02:
      return "MIME media";
    case 0x03:
      return "Absolute URI";
    case 0x04:
      return "NFC Forum external type";
    case 0x05:
      return "Unknown";
    case 0x06:
      return "Unchanged (chunked record)";
    default:
      return "Reserved";
  }
}

/**
 * @brief 获取 NDEF URI 标识码对应的前缀
 * @param code URI 标识码
 * @return URI 前缀
 */
const char* NdefUriPrefix(uint8_t code) {
  constexpr const char* kPrefixes[] = {"", "http://www.", "https://www.",
      "http://", "https://", "tel:", "mailto:",
      "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://",
      "smb://", "nfs://", "ftp://", "dav://", "news:", "telnet://",
      "imap:", "rtsp://", "urn:", "pop:", "sip:", "sips:", "tftp:",
      "btspp://", "btl2cap://", "btgoep://", "tcpobex://", "irdaobex://",
      "file://", "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:",
      "urn:epc:raw:", "urn:epc:", "urn:nfc:"};
  return code < sizeof(kPrefixes) / sizeof(kPrefixes[0]) ? kPrefixes[code]
                                                         : "";
}

void ParseAndPrintNdefMessage(
    const uint8_t* message, size_t message_length, uint8_t depth);

/**
 * @brief 解码并输出一个 NDEF 记录的负载
 * @param tnf NDEF Type Name Format
 * @param type 记录类型
 * @param type_length 类型长度
 * @param payload 记录负载
 * @param payload_length 负载长度
 * @param depth Smart Poster 嵌套深度
 */
void PrintNdefPayload(uint8_t tnf, const uint8_t* type, size_t type_length,
    const uint8_t* payload, size_t payload_length, uint8_t depth) {
  if (tnf == 0x01U && BytesEqualText(type, type_length, "T")) {
    if (payload_length == 0U) {
      PrintFieldLabel("Text record");
      std::printf("Empty text payload\n");
      return;
    }
    const uint8_t status = payload[0];
    const size_t language_length = status & 0x3FU;
    if (1U + language_length > payload_length) {
      PrintFieldLabel("Text record");
      std::printf("Malformed language field\n");
      return;
    }
    PrintFieldLabel("Text language");
    PrintEscapedText(payload + 1U, language_length);
    std::printf("\n");
    PrintFieldLabel("Text encoding");
    const bool utf16 = (status & 0x80U) != 0U;
    std::printf("%s\n", utf16 ? "UTF-16" : "UTF-8");
    PrintFieldLabel("Text content");
    const uint8_t* text = payload + 1U + language_length;
    const size_t text_length = payload_length - 1U - language_length;
    if (utf16) {
      PrintUtf16Text(text, text_length);
    } else {
      PrintEscapedText(text, text_length);
    }
    std::printf("\n");
    return;
  }

  if (tnf == 0x01U && BytesEqualText(type, type_length, "U")) {
    PrintFieldLabel("URI / website");
    if (payload_length == 0U) {
      std::printf("Empty URI payload\n");
    } else {
      std::printf("%s", NdefUriPrefix(payload[0]));
      for (size_t index = 1U; index < payload_length; ++index) {
        const uint8_t value = payload[index];
        std::printf("%c", value >= 0x20U && value != 0x7FU
                              ? static_cast<int>(value)
                              : '.');
      }
      std::printf("\n");
    }
    return;
  }

  if (tnf == 0x01U && BytesEqualText(type, type_length, "Sp")) {
    PrintFieldLabel("Smart Poster");
    std::printf("Contains %u bytes of nested NDEF records\n",
        static_cast<unsigned int>(payload_length));
    if (depth < 2U) {
      ParseAndPrintNdefMessage(payload, payload_length, depth + 1U);
    }
    return;
  }

  if (tnf == 0x02U) {
    PrintFieldLabel("MIME type");
    PrintEscapedText(type, type_length);
    std::printf("\n");
    if (LooksLikeText(payload, payload_length)) {
      PrintFieldLabel("MIME text content");
      PrintEscapedText(payload, payload_length);
      std::printf("\n");
    } else {
      PrintFieldLabel("MIME payload");
      std::printf("%u application-specific bytes (not dumped)\n",
          static_cast<unsigned int>(payload_length));
    }
    return;
  }

  if (tnf == 0x03U) {
    PrintFieldLabel("Absolute URI");
    PrintEscapedText(type, type_length);
    std::printf("\n");
    return;
  }

  if (tnf == 0x04U) {
    PrintFieldLabel("External type");
    PrintEscapedText(type, type_length);
    std::printf("\n");
    if (LooksLikeText(payload, payload_length)) {
      PrintFieldLabel("External text content");
      PrintEscapedText(payload, payload_length);
      std::printf("\n");
    }
    return;
  }

  PrintFieldLabel("Payload interpretation");
  std::printf("No standard human-readable decoder; %u bytes not dumped\n",
      static_cast<unsigned int>(payload_length));
}

/**
 * @brief 解析并输出一条 NDEF 消息中的全部记录
 * @param message NDEF 消息
 * @param message_length 消息长度
 * @param depth Smart Poster 嵌套深度
 */
void ParseAndPrintNdefMessage(
    const uint8_t* message, size_t message_length, uint8_t depth) {
  if (message == nullptr || message_length == 0U) {
    std::printf("  NDEF message is empty.\n");
    return;
  }

  size_t offset = 0;
  uint32_t record_number = 0;
  bool message_ended = false;
  while (offset < message_length && record_number < 32U) {
    const uint8_t header = message[offset++];
    const bool message_begin = (header & 0x80U) != 0U;
    const bool message_end = (header & 0x40U) != 0U;
    const bool chunked = (header & 0x20U) != 0U;
    const bool short_record = (header & 0x10U) != 0U;
    const bool id_present = (header & 0x08U) != 0U;
    const uint8_t tnf = header & 0x07U;

    if (offset >= message_length) {
      std::printf("  [WARNING] Truncated NDEF record header.\n");
      return;
    }
    const size_t type_length = message[offset++];

    uint32_t payload_length = 0;
    if (short_record) {
      if (offset >= message_length) {
        std::printf("  [WARNING] Missing short-record payload length.\n");
        return;
      }
      payload_length = message[offset++];
    } else {
      if (message_length - offset < 4U) {
        std::printf("  [WARNING] Missing NDEF payload length.\n");
        return;
      }
      payload_length = static_cast<uint32_t>(message[offset]) << 24U |
                       static_cast<uint32_t>(message[offset + 1U]) << 16U |
                       static_cast<uint32_t>(message[offset + 2U]) << 8U |
                       message[offset + 3U];
      offset += 4U;
    }

    size_t id_length = 0;
    if (id_present) {
      if (offset >= message_length) {
        std::printf("  [WARNING] Missing NDEF ID length.\n");
        return;
      }
      id_length = message[offset++];
    }

    const size_t remaining = message_length - offset;
    if (type_length > remaining || id_length > remaining - type_length ||
        payload_length > remaining - type_length - id_length) {
      std::printf("  [WARNING] NDEF record length exceeds available data.\n");
      return;
    }

    const uint8_t* type = message + offset;
    offset += type_length;
    const uint8_t* id = message + offset;
    offset += id_length;
    const uint8_t* payload = message + offset;
    offset += payload_length;

    ++record_number;
    std::printf("\n  --- NDEF record #%-2" PRIu32 "%s ---\n", record_number,
        depth > 0U ? " (Smart Poster child)" : "");
    PrintFieldLabel("Record position");
    std::printf("%s%s\n", message_begin ? "Message begin" : "Continuation",
        message_end ? ", message end" : "");
    PrintFieldLabel("Type Name Format");
    std::printf("%s (TNF=%u)\n", NdefTnfName(tnf),
        static_cast<unsigned int>(tnf));
    PrintFieldLabel("Record type");
    if (type_length == 0U) {
      std::printf("Not specified\n");
    } else {
      PrintEscapedText(type, type_length);
      std::printf("\n");
    }
    if (id_length > 0U) {
      if (LooksLikeText(id, id_length)) {
        PrintFieldLabel("Record ID");
        PrintEscapedText(id, id_length);
        std::printf("\n");
      } else {
        PrintHexField("Record ID", id, id_length);
      }
    }
    PrintFieldLabel("Payload length");
    std::printf("%" PRIu32 " bytes\n", payload_length);
    if (chunked) {
      PrintFieldLabel("Chunked record");
      std::printf("Yes; chunk reassembly is not supported\n");
    } else {
      PrintNdefPayload(
          tnf, type, type_length, payload, payload_length, depth);
    }

    if (message_end) {
      message_ended = true;
      break;
    }
  }

  if (!message_ended) {
    std::printf("  [WARNING] NDEF message ended without the ME flag.\n");
  }
}

/**
 * @brief 解析并输出 Type 2 Tag 数据区中的 TLV 和 NDEF 信息
 * @param data Type 2 Tag 数据区
 * @param data_length 可用数据长度
 */
void ParseAndPrintType2Tlvs(const uint8_t* data, size_t data_length) {
  size_t offset = 0;
  uint32_t tlv_number = 0;
  bool ndef_found = false;
  bool terminator_found = false;

  while (offset < data_length) {
    const uint8_t type = data[offset++];
    if (type == kType2NullTlv) {
      continue;
    }
    if (type == kType2TerminatorTlv) {
      terminator_found = true;
      break;
    }
    if (offset >= data_length) {
      std::printf("  [WARNING] TLV length field is missing.\n");
      break;
    }

    size_t value_length = data[offset++];
    if (value_length == 0xFFU) {
      if (data_length - offset < 2U) {
        std::printf("  [WARNING] Extended TLV length is incomplete.\n");
        break;
      }
      value_length = static_cast<size_t>(data[offset]) << 8U |
                     data[offset + 1U];
      offset += 2U;
    }
    if (value_length > data_length - offset) {
      std::printf("  [WARNING] TLV value exceeds the readable tag memory.\n");
      break;
    }

    const uint8_t* value = data + offset;
    offset += value_length;
    ++tlv_number;
    PrintFieldLabel("TLV entry");
    std::printf("#%" PRIu32 ", %u bytes - ", tlv_number,
        static_cast<unsigned int>(value_length));
    switch (type) {
      case kType2LockControlTlv:
        std::printf("Lock Control TLV\n");
        if (value_length >= 3U) {
          PrintFieldLabel("Dynamic lock area");
          std::printf("page %u, byte offset %u, %u lock bits\n",
              static_cast<unsigned int>(value[0] >> 4U),
              static_cast<unsigned int>(value[0] & 0x0FU),
              static_cast<unsigned int>(value[1]));
        }
        break;
      case kType2MemoryControlTlv:
        std::printf("Memory Control TLV\n");
        if (value_length >= 3U) {
          PrintFieldLabel("Reserved memory area");
          std::printf("page %u, byte offset %u, %u bytes\n",
              static_cast<unsigned int>(value[0] >> 4U),
              static_cast<unsigned int>(value[0] & 0x0FU),
              static_cast<unsigned int>(value[1]));
        }
        break;
      case kType2NdefMessageTlv:
        std::printf("NDEF Message TLV\n");
        ndef_found = true;
        PrintFieldLabel("NDEF message size");
        std::printf("%u bytes\n", static_cast<unsigned int>(value_length));
        ParseAndPrintNdefMessage(value, value_length, 0);
        break;
      case kType2ProprietaryTlv:
        std::printf("Proprietary TLV (content not dumped)\n");
        break;
      default:
        std::printf("Unknown or reserved TLV type 0x%02X\n",
            static_cast<unsigned int>(type));
        break;
    }
  }

  if (!ndef_found) {
    PrintFieldLabel("NDEF status");
    std::printf("No NDEF message found; the tag may be blank or proprietary\n");
  }
  PrintFieldLabel("TLV terminator");
  std::printf("%s\n", terminator_found ? "Present" : "Not found");
}

/**
 * @brief 读取 Type 2 Tag 存储区并输出 Capability Container、TLV 和 NDEF
 */
void ReadAndPrintType2TagContent() {
  std::printf("\n[ Type 2 Tag memory and NDEF content ]\n");
  uint8_t first_pages[RFAL_T2T_READ_DATA_LEN] = {};
  uint16_t received_length = 0;
  ReturnCode result = rfalT2TPollerRead(
      0, first_pages, sizeof(first_pages), &received_length);
  if (result != RFAL_ERR_NONE || received_length < sizeof(first_pages)) {
    PrintFieldLabel("Tag memory read");
    std::printf("Failed: %s (code %u, received %u bytes)\n",
        RfalErrorName(result), static_cast<unsigned int>(result),
        static_cast<unsigned int>(received_length));
    return;
  }

  PrintHexField("Static lock bytes", first_pages + 10U, 2U);
  const uint8_t* capability = first_pages + 12U;
  PrintFieldLabel("NDEF capability marker");
  std::printf("0x%02X - %s\n", static_cast<unsigned int>(capability[0]),
      capability[0] == kType2NdefMagic ? "NFC Forum formatted"
                                       : "Not NFC Forum NDEF formatted");
  if (capability[0] != kType2NdefMagic) {
    PrintFieldLabel("Tag content");
    std::printf("No standard Type 2 Capability Container; NDEF cannot be decoded\n");
    return;
  }

  PrintFieldLabel("Type 2 mapping version");
  std::printf("%u.%u\n", static_cast<unsigned int>(capability[1] >> 4U),
      static_cast<unsigned int>(capability[1] & 0x0FU));
  const size_t data_area_size = static_cast<size_t>(capability[2]) * 8U;
  PrintFieldLabel("NDEF data area capacity");
  std::printf("%u bytes\n", static_cast<unsigned int>(data_area_size));
  const uint8_t read_access = capability[3] >> 4U;
  const uint8_t write_access = capability[3] & 0x0FU;
  PrintFieldLabel("Read access");
  std::printf("%s\n", read_access == 0U ? "Open without authentication"
                                        : "Reserved / restricted");
  PrintFieldLabel("Write access");
  std::printf("%s\n", write_access == 0U
                           ? "Open without authentication"
                           : (write_access == 0x0FU ? "Read-only"
                                                    : "Protected / proprietary"));
  if (data_area_size == 0U) {
    PrintFieldLabel("NDEF status");
    std::printf("Capability Container reports zero data capacity\n");
    return;
  }

  const size_t total_memory_size = kType2HeaderSize + data_area_size;
  std::unique_ptr<uint8_t[]> memory(
      new (std::nothrow) uint8_t[total_memory_size]);
  if (memory == nullptr) {
    PrintFieldLabel("Tag memory buffer");
    std::printf("Allocation of %u bytes failed\n",
        static_cast<unsigned int>(total_memory_size));
    return;
  }
  std::memcpy(memory.get(), first_pages, sizeof(first_pages));

  size_t bytes_read = sizeof(first_pages);
  uint16_t selected_sector = 0;
  while (bytes_read < total_memory_size) {
    const size_t absolute_page = bytes_read / RFAL_T2T_BLOCK_LEN;
    const uint16_t required_sector =
        static_cast<uint16_t>(absolute_page / 256U);
    if (required_sector != selected_sector) {
      result = rfalT2TPollerSectorSelect(
          static_cast<uint8_t>(required_sector));
      if (result != RFAL_ERR_NONE) {
        PrintFieldLabel("Sector selection");
        std::printf("Sector %u failed: %s (code %u)\n",
            static_cast<unsigned int>(required_sector), RfalErrorName(result),
            static_cast<unsigned int>(result));
        break;
      }
      selected_sector = required_sector;
    }

    uint8_t read_buffer[RFAL_T2T_READ_DATA_LEN] = {};
    received_length = 0;
    result = rfalT2TPollerRead(static_cast<uint8_t>(absolute_page % 256U),
        read_buffer, sizeof(read_buffer), &received_length);
    if (result != RFAL_ERR_NONE || received_length < sizeof(read_buffer)) {
      PrintFieldLabel("Tag memory read");
      std::printf("Stopped at page %u: %s (code %u, received %u bytes)\n",
          static_cast<unsigned int>(absolute_page), RfalErrorName(result),
          static_cast<unsigned int>(result),
          static_cast<unsigned int>(received_length));
      break;
    }

    const size_t copy_length = std::min<size_t>(
        sizeof(read_buffer), total_memory_size - bytes_read);
    std::memcpy(memory.get() + bytes_read, read_buffer, copy_length);
    bytes_read += copy_length;
  }

  PrintFieldLabel("Readable tag memory");
  std::printf("%u of %u bytes\n", static_cast<unsigned int>(bytes_read),
      static_cast<unsigned int>(total_memory_size));
  if (bytes_read <= kType2HeaderSize) {
    return;
  }
  ParseAndPrintType2Tlvs(memory.get() + kType2HeaderSize,
      std::min(data_area_size, bytes_read - kType2HeaderSize));
}

/**
 * @brief 获取 ST25R3916 平台错误的可读说明
 * @param error 平台错误
 * @return 平台错误对应的英文说明
 */
const char* PlatformErrorName(stsw::PlatformError error) {
  switch (error) {
    case stsw::PlatformError::kNone:
      return "No platform error";
    case stsw::PlatformError::kInvalidConfiguration:
      return "Invalid platform configuration";
    case stsw::PlatformError::kAlreadyInUse:
      return "ST25R3916 platform is already in use";
    case stsw::PlatformError::kResourceAllocationFailed:
      return "Platform resource allocation failed";
    case stsw::PlatformError::kGpioFailed:
      return "GPIO operation failed";
    case stsw::PlatformError::kSpiFailed:
      return "SPI communication failed";
    case stsw::PlatformError::kI2cFailed:
      return "I2C communication failed";
    case stsw::PlatformError::kInterruptFailed:
      return "Interrupt handling failed";
    case stsw::PlatformError::kAssertionFailed:
      return "RFAL assertion failed";
    case stsw::PlatformError::kOfficialErrorHandler:
      return "RFAL official error handler was invoked";
    default:
      return "Unknown platform error";
  }
}

/**
 * @brief 判断当前检测到的卡片是否与上一次相同
 * @param fingerprint 上一次卡片指纹
 * @param device 当前卡片
 * @return 卡片类型和 NFC 标识符相同返回 true，否则返回 false
 */
bool IsSameCard(
    const CardFingerprint& fingerprint, const rfalNfcDevice& device) {
  if (!fingerprint.valid || fingerprint.type != device.type) {
    return false;
  }
  const uint8_t id_length = static_cast<uint8_t>(std::min<size_t>(
      device.nfcidLen, kTrackedNfcIdCapacity));
  if (fingerprint.nfcid_length != id_length) {
    return false;
  }
  return id_length == 0U ||
         (device.nfcid != nullptr &&
             std::memcmp(fingerprint.nfcid, device.nfcid, id_length) == 0);
}

/**
 * @brief 保存当前卡片的类型和 NFC 标识符
 * @param device 当前卡片
 * @param fingerprint 用于保存卡片指纹的结构体
 */
void RememberCard(
    const rfalNfcDevice& device, CardFingerprint* fingerprint) {
  if (fingerprint == nullptr) {
    return;
  }
  fingerprint->type = device.type;
  fingerprint->nfcid_length = static_cast<uint8_t>(std::min<size_t>(
      device.nfcidLen, kTrackedNfcIdCapacity));
  std::memset(fingerprint->nfcid, 0, sizeof(fingerprint->nfcid));
  if (device.nfcid != nullptr && fingerprint->nfcid_length > 0U) {
    std::memcpy(
        fingerprint->nfcid, device.nfcid, fingerprint->nfcid_length);
  }
  fingerprint->valid = true;
}

/**
 * @brief 计算两个 FreeRTOS 时刻之间经过的毫秒数
 * @param start_tick 起始时刻
 * @param end_tick 结束时刻
 * @return 经过的毫秒数
 */
uint32_t ElapsedMilliseconds(TickType_t start_tick, TickType_t end_tick) {
  return static_cast<uint32_t>((end_tick - start_tick) * portTICK_PERIOD_MS);
}

/**
 * @brief 创建 NFC 轮询和发现参数
 * @return 支持 NFC-A、B、F、V 和 ST25TB 的发现参数
 */
rfalNfcDiscoverParam CreateDiscoveryParameters() {
  rfalNfcDiscoverParam parameters = {};
  parameters.compMode = RFAL_COMPLIANCE_MODE_NFC;
  parameters.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B |
                          RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V |
                          RFAL_NFC_POLL_TECH_ST25TB;
  parameters.totalDuration = 1000;
  parameters.devLimit = 1;
  parameters.maxBR = RFAL_BR_848;
  parameters.nfcfBR = RFAL_BR_212;
  parameters.ap2pBR = RFAL_BR_424;
  parameters.notifyCb = nullptr;
  parameters.wakeupEnabled = false;
  parameters.wakeupConfigDefault = true;
  return parameters;
}

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
/**
 * @brief 使用键盘扩展板上的 SPI、INT 和 CS 引脚创建 ST25R3916 驱动
 * @return ST25R3916 驱动所有权
 */
std::unique_ptr<stsw::St25r3916x> CreateNfcDriver() {
  auto& board_driver = common::GetDriver();
  auto spi_bus = std::make_shared<cpp_bus_driver::HardwareSpi>(
      board_driver.bus().sx1262_spi_bus, 1);
  return std::make_unique<stsw::St25r3916x>(spi_bus,
      common::board::keyboard::gpio::t_mix_rf::st25r3916::kInt,
      common::board::keyboard::gpio::t_mix_rf::st25r3916::kCs);
}
#elif defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4_AIR)
#else
#error "Unsupported board for the ST25R3916 example"
#endif

}  // namespace

extern "C" void app_main(void) {
  std::printf("\n%s\n", kSectionLine);
  std::printf("                 ST25R3916 NFC CARD READER\n");
  std::printf("%s\n", kSectionLine);
  std::printf("Board                    : %s\n", common::kBoardName);
  std::printf("Reader                   : ST25R3916\n");
  std::printf("Supported technologies   : NFC-A, NFC-B, NFC-F, NFC-V, ST25TB\n");
  std::printf("Maximum configured rate  : 848 kbit/s\n");
  std::printf("%s\n", kSectionLine);
  std::printf("Initializing board and NFC reader...\n");
  if (!common::InitDriver()) {
    std::printf("[ERROR] Board initialization failed: %s\n",
        common::kBoardName);
    return;
  }

#if defined(CONFIG_LILYGO_DEVICE_DRIVER_T_DISPLAY_P4)
  auto nfc_owner = CreateNfcDriver();
  auto* nfc = nfc_owner.get();
  ReturnCode result = nfc->Init();
  if (result != RFAL_ERR_NONE) {
    const stsw::PlatformError platform_error = nfc->platform_error();
    std::printf("[ERROR] ST25R3916 initialization failed\n");
    std::printf("        RFAL     : %s (code %u)\n", RfalErrorName(result),
        static_cast<unsigned int>(result));
    std::printf("        Platform : %s (code %u)\n",
        PlatformErrorName(platform_error),
        static_cast<unsigned int>(platform_error));
    return;
  }
#else
  auto& board_driver = common::GetDriver();
  auto* nfc = board_driver.chip().st25r3916.get();
  if (!board_driver.IsSt25r3916Ready() || nfc == nullptr) {
    const auto& status = board_driver.status().st25r3916;
    std::printf("[ERROR] ST25R3916 initialization failed\n");
    std::printf("        RFAL     : %s (code %u)\n",
        RfalErrorName(status.result),
        static_cast<unsigned int>(status.result));
    std::printf("        Platform : %s (code %u)\n",
        PlatformErrorName(status.platform_error),
        static_cast<unsigned int>(status.platform_error));
    return;
  }
  ReturnCode result = RFAL_ERR_NONE;
#endif

  rfalNfcDiscoverParam discovery = CreateDiscoveryParameters();
  result = rfalNfcDiscover(&discovery);
  if (result != RFAL_ERR_NONE) {
    std::printf("[ERROR] NFC discovery could not start: %s (code %u)\n",
        RfalErrorName(result), static_cast<unsigned int>(result));
    return;
  }

  std::printf("Initialization complete. NFC discovery is running.\n");
  std::printf("Place one NFC card near the antenna and keep it steady.\n\n");

  CardFingerprint last_card;
  TickType_t last_card_seen_tick = xTaskGetTickCount();
  uint32_t report_number = 0;
  while (true) {
    nfc->NfcWorker();
    const stsw::PlatformError platform_error = nfc->platform_error();
    if (platform_error != stsw::PlatformError::kNone) {
      std::printf("[ERROR] ST25R3916 platform failure: %s (code %u)\n",
          PlatformErrorName(platform_error),
          static_cast<unsigned int>(platform_error));
      break;
    }

    const rfalNfcState state = rfalNfcGetState();
    if (rfalNfcIsDevActivated(state)) {
      rfalNfcDevice* device = nullptr;
      result = rfalNfcGetActiveDevice(&device);
      if (result == RFAL_ERR_NONE && device != nullptr) {
        const TickType_t now = xTaskGetTickCount();
        const bool same_card = IsSameCard(last_card, *device);
        const bool card_was_absent =
            !last_card.valid ||
            ElapsedMilliseconds(last_card_seen_tick, now) >=
                kCardRemovalTimeoutMs;
        if (!same_card || card_was_absent) {
          PrintDevice(*device, ++report_number);
        }
        RememberCard(*device, &last_card);
        last_card_seen_tick = now;
      } else {
        std::printf("[WARNING] Active NFC device details are unavailable: "
                    "%s (code %u)\n",
            RfalErrorName(result), static_cast<unsigned int>(result));
      }

      vTaskDelay(pdMS_TO_TICKS(500));

      result = rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
      if (result != RFAL_ERR_NONE) {
        std::printf("[WARNING] NFC discovery restart failed: %s (code %u)\n",
            RfalErrorName(result), static_cast<unsigned int>(result));
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    } else if (rfalNfcIsInDiscovery(state) && last_card.valid &&
               ElapsedMilliseconds(
                   last_card_seen_tick, xTaskGetTickCount()) >=
                   kCardRemovalTimeoutMs) {
      last_card.valid = false;
      std::printf("Card removed. Ready for the next NFC card.\n\n");
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
