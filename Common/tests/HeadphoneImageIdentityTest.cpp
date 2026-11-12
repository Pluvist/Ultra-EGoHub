#include "HeadphoneImageIdentity.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using HuaweiHeadphones::Model;
using namespace HuaweiHeadphones::Images;
static void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main() { try {
    auto pro = ConfirmedIdentity(Model::Pro5, "00016D", "17");
    Check(pro.has_value(), "confirmed Pro5 model/color accepted");
    Check(ConfigUrl(*pro) == "https://smarthome-drcn.dbankcdn.cn/device/guide/AAM001/00016D/00016D.json", "fixed official configuration URI");
    Check(ArchiveUrl(*pro) == "https://smarthome-drcn.dbankcdn.cn/device/guide/AAM001/00016D/00016D_17.zip", "device color retained instead of default 01");
    Check(CacheKey(*pro) == "00016D_17", "cache isolates models and colors");
    Check(!ConfirmedIdentity(Model::Pro5, "00016D", ""), "missing device color must not fall back to default");
    Check(!ConfirmedIdentity(Model::Pro5, "00016d", "17"), "noncanonical ID rejected");
    Check(!ConfirmedIdentity(Model::Pro5, "000153", "17"), "other model cannot borrow a Pro5 identity");
    Check(!ConfirmedIdentity(Model::Unknown, "00000A", "01"), "common fallback identity rejected");
    Check(!ConfirmedIdentity(Model::Unknown, "000000", "01"), "empty model rejected");
    Check(!ConfirmedIdentity(Model::Unknown, "00016D", "17?x=1"), "query injection rejected");
    Check(!ConfigUrl(Identity{"../../", "17"}), "URI revalidates fabricated identity fields");
    Check(!ArchiveUrl(Identity{"00016D", "../17"}), "path injection rejected");
    Check(ConfirmedIdentity(Model::Unknown, "000177", "01").has_value(), "confirmed new device may request its own official resource");
    Check(ConfirmedIdentity(Model::Clip2, "000173", "00").has_value(), "collector edition keeps its own resource ID");
    Check(!ConfirmedIdentity(Model::Pro4, "00016D", "17"), "uncatalogued model cannot substitute a known model");
    Check(CatalogModelId(Model::Buds3) == "000027", "legacy directory available for explicit selection");
    Check(!CatalogModelId(Model::Pro4) && !CatalogModelId(Model::Clip), "missing catalog identities are not guessed");
    Check(SafeZipEntryName("00016D_17/"), "ordinary ZIP directory permitted");
    Check(!SafeZipEntryName("../iv_device_logo.png"), "parent traversal rejected");
    Check(!SafeZipEntryName("00016D_17\\..\\iv_device_logo.png"), "Windows traversal rejected");
    Check(!SafeZipEntryName("C:\\iv_device_logo.png"), "drive path rejected");
    Check(!SafeZipEntryName("\\\\server\\image.png"), "UNC path rejected");
    Check(!SafeZipEntryName("/iv_device_logo.png"), "absolute path rejected");
    Check(!SafeZipEntryName(std::string("a\0b",3)), "embedded null rejected");
    Check(EntryPriority("00016D_17/IV_DEVICE_LOGO.PNG", Part::Main) == 0, "exact main-image basename case-insensitive");
    Check(!EntryPriority("00016D_17/iv_device_logo_id_bar.png", Part::Main), "small similarly named asset cannot replace HD main image");
    Check(!EntryPriority("00016D_17/my_iv_device_logo.png", Part::Main), "substring match cannot select unrelated asset");
    Check(!EntryPriority("00016D_17/iv_device_logo.png.exe", Part::Main), "extension suffix rejected");
    Check(!EntryPriority("00016D_17/iv_device_logo.png/", Part::Main), "directories cannot be selected as PNGs");
    std::vector<std::string> entries{"a/device_icon.png", "00016D_17/iv_device_logo.png", "a/iv_device_logo.png", "a/find_left_icon.png", "a/find_right_icon.png"};
    Check(SelectEntry(entries, Part::Main) == 2, "HD basename priority then shortest path determines selection");
    Check(SelectEntry(entries, Part::Left) == 3 && SelectEntry(entries, Part::Right) == 4, "left/right never confused");
    entries = {"a/iv_device_logo_id_pad.png", "../iv_device_logo.png"};
    Check(!SelectEntry(entries, Part::Main), "no trusted main image yields fallback rather than guessed image");
    std::cout << "Headphone image identity, official URI and ZIP selection policy passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
