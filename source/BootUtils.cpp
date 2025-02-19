#include "BootUtils.h"
#include "ACTAccountInfo.h"
#include "DrawUtils.h"
#include "MenuUtils.h"
#include "logger.h"
#include <codecvt>
#include <coreinit/filesystem_fsa.h>
#include <locale>
#include <malloc.h>
#include <memory>
#include <mocha/mocha.h>
#include <nn/act.h>
#include <nn/cmpt/cmpt.h>
#include <padscore/kpad.h>
#include <sstream>
#include <string>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <vector>

void handleAccountSelection();

void bootWiiUMenu() {
    nn::act::Initialize();
    nn::act::SlotNo slot        = nn::act::GetSlotNo();
    nn::act::SlotNo defaultSlot = nn::act::GetDefaultAccount();
    nn::act::Finalize();

    if (defaultSlot) { //normal menu boot
        SYSLaunchMenu();
    } else { //show mii select
        _SYSLaunchMenuWithCheckingAccount(slot);
    }
}

void bootHomebrewLauncher() {
    handleAccountSelection();

    uint64_t titleId = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_MII_MAKER);
    _SYSLaunchTitleWithStdArgsInNoSplash(titleId, nullptr);
}

void bootWiiuTitle(const std::string &hexId) {
    uint64_t titleId;
    std::stringstream ss;
    ss << std::hex << hexId;
    ss >> titleId;

    handleAccountSelection();

    titleId = _SYSGetSystemApplicationTitleId(static_cast<SYSTEM_APP_ID>(titleId));
    _SYSLaunchTitleWithStdArgsInNoSplash(titleId, nullptr);
}

void handleAccountSelection() {
    nn::act::Initialize();
    nn::act::SlotNo defaultSlot = nn::act::GetDefaultAccount();

    if (!defaultSlot) { // No default account is set.
        std::vector<std::shared_ptr<AccountInfo>> accountInfoList;
        for (int32_t i = 0; i < 13; i++) {
            if (!nn::act::IsSlotOccupied(i)) {
                continue;
            }
            char16_t nameOut[nn::act::MiiNameSize];
            std::shared_ptr<AccountInfo> accountInfo = std::make_shared<AccountInfo>();
            accountInfo->slot                        = i;
            auto result                              = nn::act::GetMiiNameEx(reinterpret_cast<int16_t *>(nameOut), i);
            if (result.IsSuccess()) {
                std::u16string source;
                std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
                accountInfo->name = convert.to_bytes((char16_t *) nameOut);
            } else {
                accountInfo->name = "[UNKNOWN]";
            }
            accountInfo->isNetworkAccount = nn::act::IsNetworkAccountEx(i);
            if (accountInfo->isNetworkAccount) {
                nn::act::GetAccountIdEx(accountInfo->accountId, i);
            }

            uint32_t imageSize = 0;
            result             = nn::act::GetMiiImageEx(&imageSize, accountInfo->miiImageBuffer, sizeof(accountInfo->miiImageBuffer), 0, i);
            if (result.IsSuccess()) {
                accountInfo->miiImageSize = imageSize;
            }
            accountInfoList.push_back(accountInfo);
        }

        if (accountInfoList.size() > 0) {
            auto slot = handleAccountSelectScreen(accountInfoList);

            DEBUG_FUNCTION_LINE("Load slot %d", slot);
            nn::act::LoadConsoleAccount(slot, 0, nullptr, false);
        }
    }
    nn::act::Finalize();
}

void launchvWiiTitle(uint64_t titleId) {
    // we need to init kpad for cmpt
    KPADInit();

    // Try to find a screen type that works
    CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_BOTH);
    if (CMPTCheckScreenState() < 0) {
        CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_DRC);
        if (CMPTCheckScreenState() < 0) {
            CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_TV);
        }
    }

    uint32_t dataSize = 0;
    CMPTGetDataSize(&dataSize);

    void *dataBuffer = memalign(0x40, dataSize);

    if (titleId == 0) {
        CMPTLaunchMenu(dataBuffer, dataSize);
    } else {
        CMPTLaunchTitle(dataBuffer, dataSize, titleId);
    }

    free(dataBuffer);
}

void bootvWiiMenu() {
    launchvWiiTitle(0);
}

uint64_t getVWiiTitleId(const std::string& hexId) {
    // fall back to booting the vWii system menu if anything fails
    uint64_t titleId = 0;

    FSAInit();
    auto client = FSAAddClient(nullptr);
    if (client > 0) {
        if (Mocha_UnlockFSClientEx(client) == MOCHA_RESULT_SUCCESS) {
            // mount the slccmpt
            if (FSAMount(client, "/dev/slccmpt01", "/vol/storage_slccmpt01", FSA_MOUNT_FLAG_GLOBAL_MOUNT, nullptr, 0) >= 0) {
                FSStat stat;

                const std::string hexValue{ hexId.size() > 2 ? hexId.substr(2) : hexId };
                const std::string titleString{ "/vol/storage_slccmpt01/title/00010001/" + hexValue + "/content/00000000.app" };
                if (FSAGetStat(client, titleString.c_str(), &stat) >= 0) {
                    std::stringstream ss;
                    ss << std::hex << hexId;
                    ss >> titleId;
                    titleId |= 0x0001000100000000L;
                } else {
                    DEBUG_FUNCTION_LINE("Cannot find title 0x%s", hexId.c_str());
                }
                FSAUnmount(client, "/vol/storage_slccmpt01", static_cast<FSAUnmountFlags>(2));
            } else {
                DEBUG_FUNCTION_LINE_ERR("Failed to mount slccmpt01");
            }
        } else {
            DEBUG_FUNCTION_LINE_ERR("Failed to unlock FSClient");
        }
        FSADelClient(client);
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to add FSAClient");
    }

    return titleId;
}

uint64_t getVWiiHBLTitleId() {
    // Try 'OHBC' first and if it fails, try 'LULZ'
    uint64_t titleId = getVWiiTitleId("4f484243");
    if (titleId == 0) {
        titleId = getVWiiTitleId("4c554c5a");
    }

    return titleId;
}

void bootHomebrewChannel() {
    uint64_t titleId = getVWiiHBLTitleId();
    DEBUG_FUNCTION_LINE("Launching vWii title %016llx", titleId);
    launchvWiiTitle(titleId);
}
