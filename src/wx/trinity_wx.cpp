// Copyright (c) 2026 Trinity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/valnum.h>

extern "C" {
#include "wallet.h"
#include "main.h"
#include "init.h"
#include "net.h"
#include "util.h"
#include "bitcoinrpc.h"
#include "base58.h"
}

#include <boost/thread.hpp>
#include "json/json_spirit_writer.h"

namespace {
std::string FormatAmount(int64 amount)
{
    return FormatMoney(amount);
}

std::string GetAlgoLabel()
{
    return GetAlgoName(miningAlgo);
}

std::string FormatTime(int64 timeValue)
{
    wxDateTime dt = wxDateTime::FromTimeT(static_cast<time_t>(timeValue));
    return dt.FormatISOCombined(' ').ToStdString();
}

bool ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    wxMessageBox(wxString::FromUTF8(message.c_str()), wxString::FromUTF8(caption.c_str()));
    return true;
}

bool ThreadSafeAskFee(int64)
{
    return true;
}

void InitMessage(const std::string &message)
{
    wxLogMessage("%s", message);
}

std::string Translate(const char* psz)
{
    return std::string(psz);
}
} // namespace

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);

class OverviewPanel : public wxPanel
{
public:
    OverviewPanel(wxWindow *parent, wxStatusBar *statusBarIn)
        : wxPanel(parent), statusBar(statusBarIn)
    {
        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 8);
        grid->AddGrowableCol(1, 1);

        AddRow(grid, "Client Version:", clientVersion);
        AddRow(grid, "Protocol Version:", protocolVersion);
        AddRow(grid, "Balance:", balance);
        AddRow(grid, "Blocks:", blocks);
        AddRow(grid, "Connections:", connections);
        AddRow(grid, "PoW Algo:", powAlgo);
        AddRow(grid, "Difficulty:", difficulty);
        AddRow(grid, "Network Hashrate:", hashrate);

        wxButton *refreshButton = new wxButton(this, wxID_ANY, "Refresh Overview");
        refreshButton->Bind(wxEVT_BUTTON, &OverviewPanel::OnRefresh, this);

        mainSizer->Add(grid, 0, wxALL | wxEXPAND, 12);
        mainSizer->Add(refreshButton, 0, wxALL | wxALIGN_RIGHT, 12);

        SetSizer(mainSizer);
    }

    void Refresh()
    {
        clientVersion->SetLabel(FormatFullVersion());
        protocolVersion->SetLabel(wxString::Format("%d", PROTOCOL_VERSION));
        balance->SetLabel(FormatAmount(pwalletMain ? pwalletMain->GetBalance() : 0));
        blocks->SetLabel(wxString::Format("%d", nBestHeight));
        connections->SetLabel(wxString::Format("%lu", vNodes.size()));
        powAlgo->SetLabel(GetAlgoLabel());
        difficulty->SetLabel(wxString::Format("%.8f", GetDifficulty(NULL, miningAlgo)));
        hashrate->SetLabel(wxString::Format("%.0f H/s", dHashesPerSec));
    }

private:
    void AddRow(wxFlexGridSizer *grid, const wxString &labelText, wxStaticText *&valueLabel)
    {
        wxStaticText *label = new wxStaticText(this, wxID_ANY, labelText);
        valueLabel = new wxStaticText(this, wxID_ANY, "-");
        grid->Add(label, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        grid->Add(valueLabel, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    wxStatusBar *statusBar;
    wxStaticText *clientVersion;
    wxStaticText *protocolVersion;
    wxStaticText *balance;
    wxStaticText *blocks;
    wxStaticText *connections;
    wxStaticText *powAlgo;
    wxStaticText *difficulty;
    wxStaticText *hashrate;
};

class WalletPanel : public wxPanel
{
public:
    WalletPanel(wxWindow *parent, wxStatusBar *statusBarIn)
        : wxPanel(parent), statusBar(statusBarIn)
    {
        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer *balanceSizer = new wxBoxSizer(wxHORIZONTAL);
        balanceSizer->Add(new wxStaticText(this, wxID_ANY, "Balance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        balanceText = new wxStaticText(this, wxID_ANY, "-");
        balanceSizer->Add(balanceText, 0, wxALIGN_CENTER_VERTICAL);

        wxButton *refreshButton = new wxButton(this, wxID_ANY, "Refresh Wallet");
        refreshButton->Bind(wxEVT_BUTTON, &WalletPanel::OnRefresh, this);

        wxBoxSizer *headerSizer = new wxBoxSizer(wxHORIZONTAL);
        headerSizer->Add(balanceSizer, 0, wxALIGN_CENTER_VERTICAL);
        headerSizer->AddStretchSpacer();
        headerSizer->Add(refreshButton, 0, wxALIGN_CENTER_VERTICAL);

        mainSizer->Add(headerSizer, 0, wxALL | wxEXPAND, 12);

        wxStaticBoxSizer *sendSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Send Trinity");
        wxBoxSizer *addressSizer = new wxBoxSizer(wxHORIZONTAL);
        addressSizer->Add(new wxStaticText(this, wxID_ANY, "Address:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        sendAddress = new wxTextCtrl(this, wxID_ANY);
        addressSizer->Add(sendAddress, 1, wxEXPAND);

        wxFloatingPointValidator<double> amountValidator(8, &sendAmountValue);
        amountValidator.SetRange(0.0, 21000000.0);
        wxBoxSizer *amountSizer = new wxBoxSizer(wxHORIZONTAL);
        amountSizer->Add(new wxStaticText(this, wxID_ANY, "Amount:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        sendAmount = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, amountValidator);
        amountSizer->Add(sendAmount, 0, wxRIGHT, 8);
        sendButton = new wxButton(this, wxID_ANY, "Send");
        sendButton->Bind(wxEVT_BUTTON, &WalletPanel::OnSend, this);
        amountSizer->Add(sendButton, 0, wxALIGN_CENTER_VERTICAL);

        sendSizer->Add(addressSizer, 0, wxALL | wxEXPAND, 6);
        sendSizer->Add(amountSizer, 0, wxALL | wxEXPAND, 6);
        mainSizer->Add(sendSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        wxStaticBoxSizer *addressBookSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Receive Address");
        wxBoxSizer *newAddressSizer = new wxBoxSizer(wxHORIZONTAL);
        newAddress = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        newAddressSizer->Add(newAddress, 1, wxRIGHT, 6);
        wxButton *newAddressButton = new wxButton(this, wxID_ANY, "New Address");
        newAddressButton->Bind(wxEVT_BUTTON, &WalletPanel::OnNewAddress, this);
        newAddressSizer->Add(newAddressButton, 0);
        addressBookSizer->Add(newAddressSizer, 0, wxALL | wxEXPAND, 6);
        mainSizer->Add(addressBookSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        wxStaticBoxSizer *transactionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Recent Transactions");
        transactionsList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        transactionsList->InsertColumn(0, "Date");
        transactionsList->InsertColumn(1, "Category");
        transactionsList->InsertColumn(2, "Amount");
        transactionsList->InsertColumn(3, "Address");
        transactionsList->InsertColumn(4, "TxID");
        transactionsSizer->Add(transactionsList, 1, wxEXPAND | wxALL, 6);
        mainSizer->Add(transactionsSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        wxStaticBoxSizer *receivedSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Received by Address");
        receivedList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        receivedList->InsertColumn(0, "Address");
        receivedList->InsertColumn(1, "Account");
        receivedList->InsertColumn(2, "Amount");
        receivedList->InsertColumn(3, "Confirmations");
        receivedSizer->Add(receivedList, 1, wxEXPAND | wxALL, 6);
        mainSizer->Add(receivedSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        SetSizer(mainSizer);
    }

    void Refresh()
    {
        balanceText->SetLabel(FormatAmount(pwalletMain ? pwalletMain->GetBalance() : 0));

        RefreshTransactions();
        RefreshReceived();
    }

private:
    void RefreshTransactions()
    {
        transactionsList->DeleteAllItems();
        if (!pwalletMain)
            return;

        std::list<CAccountingEntry> acentries;
        CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, "*");
        long index = 0;
        for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            CWalletTx *const pwtx = (*it).second.first;
            if (!pwtx)
                continue;

            int64 nFee = 0;
            std::string strSentAccount;
            std::list<std::pair<CTxDestination, int64> > listReceived;
            std::list<std::pair<CTxDestination, int64> > listSent;
            pwtx->GetAmounts(listReceived, listSent, nFee, strSentAccount);

            if (!listSent.empty() || nFee != 0)
            {
                BOOST_FOREACH(const PAIRTYPE(CTxDestination, int64)& s, listSent)
                {
                    long itemIndex = transactionsList->InsertItem(index, FormatTimestamp(pwtx->GetTxTime()));
                    transactionsList->SetItem(itemIndex, 1, "send");
                    transactionsList->SetItem(itemIndex, 2, FormatAmount(-s.second));
                    transactionsList->SetItem(itemIndex, 3, CBitcoinAddress(s.first).ToString());
                    transactionsList->SetItem(itemIndex, 4, pwtx->GetHash().GetHex());
                    ++index;
                }
            }

            if (listReceived.size() > 0 && pwtx->GetDepthInMainChain() >= 0)
            {
                BOOST_FOREACH(const PAIRTYPE(CTxDestination, int64)& r, listReceived)
                {
                    long itemIndex = transactionsList->InsertItem(index, FormatTimestamp(pwtx->GetTxTime()));
                    transactionsList->SetItem(itemIndex, 1, "receive");
                    transactionsList->SetItem(itemIndex, 2, FormatAmount(r.second));
                    transactionsList->SetItem(itemIndex, 3, CBitcoinAddress(r.first).ToString());
                    transactionsList->SetItem(itemIndex, 4, pwtx->GetHash().GetHex());
                    ++index;
                }
            }

            if (index >= 10)
                break;
        }
    }

    void RefreshReceived()
    {
        receivedList->DeleteAllItems();
        if (!pwalletMain)
            return;

        map<CTxDestination, int64> balances = pwalletMain->GetAddressBalances();
        long index = 0;
        BOOST_FOREACH(const PAIRTYPE(CTxDestination, int64)& entry, balances)
        {
            long itemIndex = receivedList->InsertItem(index, CBitcoinAddress(entry.first).ToString());
            std::string account;
            if (pwalletMain->mapAddressBook.count(entry.first))
                account = pwalletMain->mapAddressBook[entry.first];
            receivedList->SetItem(itemIndex, 1, account);
            receivedList->SetItem(itemIndex, 2, FormatAmount(entry.second));
            receivedList->SetItem(itemIndex, 3, "1+");
            ++index;
        }
    }

    wxString FormatTimestamp(int64 timeValue)
    {
        return wxString::FromUTF8(FormatTime(timeValue).c_str());
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    void OnNewAddress(wxCommandEvent &)
    {
        if (!pwalletMain)
        {
            statusBar->SetStatusText("Wallet not available.");
            return;
        }

        CPubKey newKey;
        if (!pwalletMain->GetKeyFromPool(newKey, false))
        {
            statusBar->SetStatusText("Keypool ran out.");
            return;
        }
        CKeyID keyID = newKey.GetID();
        pwalletMain->SetAddressBookName(keyID, "");
        newAddress->SetValue(CBitcoinAddress(keyID).ToString());
        statusBar->SetStatusText("Generated new receive address.");
    }

    void OnSend(wxCommandEvent &)
    {
        if (!pwalletMain)
        {
            statusBar->SetStatusText("Wallet not available.");
            return;
        }
        std::string address = sendAddress->GetValue().ToStdString();
        double amount = 0.0;
        if (!sendAmount->GetValue().ToDouble(&amount))
        {
            statusBar->SetStatusText("Enter a valid amount.");
            return;
        }
        if (address.empty())
        {
            statusBar->SetStatusText("Enter a destination address.");
            return;
        }

        CBitcoinAddress destAddress(address);
        if (!destAddress.IsValid())
        {
            statusBar->SetStatusText("Invalid address.");
            return;
        }

        int64 nAmount = 0;
        if (!ParseMoney(sendAmount->GetValue().ToStdString(), nAmount))
        {
            statusBar->SetStatusText("Invalid amount.");
            return;
        }

        CWalletTx wtx;
        std::string result = pwalletMain->SendMoneyToDestination(destAddress.Get(), nAmount, wtx);
        if (!result.empty())
        {
            statusBar->SetStatusText(wxString::FromUTF8(result.c_str()));
            return;
        }

        statusBar->SetStatusText("Transaction sent: " + wtx.GetHash().GetHex());
        Refresh();
    }

    wxStatusBar *statusBar;
    wxStaticText *balanceText;
    wxTextCtrl *sendAddress;
    wxTextCtrl *sendAmount;
    wxTextCtrl *newAddress;
    wxListCtrl *transactionsList;
    wxListCtrl *receivedList;
    wxButton *sendButton;
    double sendAmountValue = 0.0;
};

class ExplorerPanel : public wxPanel
{
public:
    ExplorerPanel(wxWindow *parent, wxStatusBar *statusBarIn)
        : wxPanel(parent), statusBar(statusBarIn)
    {
        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer *blockSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Block Lookup");
        wxBoxSizer *heightSizer = new wxBoxSizer(wxHORIZONTAL);
        heightSizer->Add(new wxStaticText(this, wxID_ANY, "Height:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        blockHeight = new wxSpinCtrl(this, wxID_ANY);
        blockHeight->SetRange(0, 100000000);
        heightSizer->Add(blockHeight, 0, wxRIGHT, 12);
        wxButton *heightButton = new wxButton(this, wxID_ANY, "Fetch by Height");
        heightButton->Bind(wxEVT_BUTTON, &ExplorerPanel::OnFetchHeight, this);
        heightSizer->Add(heightButton, 0);

        wxBoxSizer *hashSizer = new wxBoxSizer(wxHORIZONTAL);
        hashSizer->Add(new wxStaticText(this, wxID_ANY, "Block Hash:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        blockHash = new wxTextCtrl(this, wxID_ANY);
        hashSizer->Add(blockHash, 1, wxRIGHT, 12);
        wxButton *hashButton = new wxButton(this, wxID_ANY, "Fetch by Hash");
        hashButton->Bind(wxEVT_BUTTON, &ExplorerPanel::OnFetchHash, this);
        hashSizer->Add(hashButton, 0);

        blockSizer->Add(heightSizer, 0, wxALL | wxEXPAND, 6);
        blockSizer->Add(hashSizer, 0, wxALL | wxEXPAND, 6);

        wxStaticBoxSizer *txSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Transaction Lookup");
        wxBoxSizer *txInputSizer = new wxBoxSizer(wxHORIZONTAL);
        txInputSizer->Add(new wxStaticText(this, wxID_ANY, "TxID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        txId = new wxTextCtrl(this, wxID_ANY);
        txInputSizer->Add(txId, 1, wxRIGHT, 12);
        wxButton *txButton = new wxButton(this, wxID_ANY, "Fetch Transaction");
        txButton->Bind(wxEVT_BUTTON, &ExplorerPanel::OnFetchTransaction, this);
        txInputSizer->Add(txButton, 0);
        txSizer->Add(txInputSizer, 0, wxALL | wxEXPAND, 6);

        results = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);

        mainSizer->Add(blockSizer, 0, wxALL | wxEXPAND, 12);
        mainSizer->Add(txSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
        mainSizer->Add(results, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        SetSizer(mainSizer);
    }

    void Refresh()
    {
        results->SetValue("Use the controls above to query blocks or transactions.");
    }

private:
    void OnFetchHeight(wxCommandEvent &)
    {
        LOCK(cs_main);
        CBlockIndex *pindex = FindBlockByHeight(blockHeight->GetValue());
        if (!pindex)
        {
            statusBar->SetStatusText("Block height not found.");
            return;
        }
        blockHash->SetValue(pindex->GetBlockHash().GetHex());
        FetchBlock(pindex);
    }

    void OnFetchHash(wxCommandEvent &)
    {
        std::string hash = blockHash->GetValue().ToStdString();
        if (hash.empty())
        {
            statusBar->SetStatusText("Enter a block hash.");
            return;
        }
        uint256 hashValue;
        hashValue.SetHex(hash);
        LOCK(cs_main);
        if (!mapBlockIndex.count(hashValue))
        {
            statusBar->SetStatusText("Block not found.");
            return;
        }
        FetchBlock(mapBlockIndex[hashValue]);
    }

    void FetchBlock(const CBlockIndex *pindex)
    {
        if (!pindex)
            return;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex))
        {
            statusBar->SetStatusText("Unable to read block from disk.");
            return;
        }

        std::ostringstream summary;
        summary << "Block " << pindex->nHeight << "\n";
        summary << "Hash: " << pindex->GetBlockHash().GetHex() << "\n";
        summary << "Confirmations: " << pindex->GetDepthInMainChain() << "\n";
        summary << "Time: " << FormatTime(block.GetBlockTime()) << "\n";
        summary << "Transactions: " << block.vtx.size() << "\n";
        results->SetValue(summary.str());
    }

    void OnFetchTransaction(wxCommandEvent &)
    {
        std::string id = txId->GetValue().ToStdString();
        if (id.empty())
        {
            statusBar->SetStatusText("Enter a transaction id.");
            return;
        }
        uint256 hash;
        hash.SetHex(id);
        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(hash, tx, hashBlock, true))
        {
            statusBar->SetStatusText("Transaction not found.");
            return;
        }

        json_spirit::Object entry;
        TxToJSON(tx, hashBlock, entry);
        results->SetValue(json_spirit::write_string(json_spirit::Value(entry), true));
    }

    wxStatusBar *statusBar;
    wxSpinCtrl *blockHeight;
    wxTextCtrl *blockHash;
    wxTextCtrl *txId;
    wxTextCtrl *results;
};

class AiPanel : public wxPanel
{
public:
    AiPanel(wxWindow *parent, wxStatusBar *statusBarIn)
        : wxPanel(parent), statusBar(statusBarIn)
    {
        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText *headline = new wxStaticText(this, wxID_ANY, "Trinity AI Architecture");
        wxFont headlineFont = headline->GetFont();
        headlineFont.SetWeight(wxFONTWEIGHT_BOLD);
        headline->SetFont(headlineFont);

        description = new wxStaticText(
            this,
            wxID_ANY,
            "Network-powered intelligence uses Trinity mining signals to evolve beyond static LLM behavior. "
            "The AI layer adapts with on-chain cadence and proof-of-work energy.");

        wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 8);
        grid->AddGrowableCol(1, 1);
        AddRow(grid, "PoW Algo:", powAlgo);
        AddRow(grid, "Difficulty:", difficulty);
        AddRow(grid, "Hashrate:", hashrate);
        AddRow(grid, "Network Power Score:", powerScore);

        wxButton *refreshButton = new wxButton(this, wxID_ANY, "Refresh AI Power");
        refreshButton->Bind(wxEVT_BUTTON, &AiPanel::OnRefresh, this);

        mainSizer->Add(headline, 0, wxALL, 12);
        mainSizer->Add(description, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        mainSizer->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
        mainSizer->Add(refreshButton, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 12);

        SetSizer(mainSizer);
    }

    void Refresh()
    {
        powAlgo->SetLabel(GetAlgoLabel());
        difficulty->SetLabel(wxString::Format("%.8f", GetDifficulty(NULL, miningAlgo)));
        hashrate->SetLabel(wxString::Format("%.0f H/s", dHashesPerSec));
        double score = GetDifficulty(NULL, miningAlgo) * dHashesPerSec;
        powerScore->SetLabel(wxString::Format("%.2f", score));
    }

private:
    void AddRow(wxFlexGridSizer *grid, const wxString &labelText, wxStaticText *&valueLabel)
    {
        wxStaticText *label = new wxStaticText(this, wxID_ANY, labelText);
        valueLabel = new wxStaticText(this, wxID_ANY, "-");
        grid->Add(label, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        grid->Add(valueLabel, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    wxStatusBar *statusBar;
    wxStaticText *description;
    wxStaticText *powAlgo;
    wxStaticText *difficulty;
    wxStaticText *hashrate;
    wxStaticText *powerScore;
};

class MainFrame : public wxFrame
{
public:
    MainFrame()
        : wxFrame(NULL, wxID_ANY, "Trinity Wallet (wxWidgets)", wxDefaultPosition, wxSize(1100, 750))
    {
        CreateStatusBar();
        statusBar = GetStatusBar();
        statusBar->SetStatusText("Initializing Trinity core...");

        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        notebook = new wxNotebook(this, wxID_ANY);
        overviewPanel = new OverviewPanel(notebook, statusBar);
        walletPanel = new WalletPanel(notebook, statusBar);
        explorerPanel = new ExplorerPanel(notebook, statusBar);
        aiPanel = new AiPanel(notebook, statusBar);

        notebook->AddPage(overviewPanel, "Overview", true);
        notebook->AddPage(walletPanel, "Wallet");
        notebook->AddPage(explorerPanel, "Explorer");
        notebook->AddPage(aiPanel, "AI Power");

        mainSizer->Add(notebook, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        SetSizer(mainSizer);
    }

    void RefreshPanels()
    {
        overviewPanel->Refresh();
        walletPanel->Refresh();
        explorerPanel->Refresh();
        aiPanel->Refresh();
    }

    wxStatusBar *statusBar;
    wxNotebook *notebook;
    OverviewPanel *overviewPanel;
    WalletPanel *walletPanel;
    ExplorerPanel *explorerPanel;
    AiPanel *aiPanel;
};

class TrinityWxApp : public wxApp
{
public:
    bool OnInit() override
    {
        fHaveGUI = true;
        ParseParameters(argc, argv);
        ReadConfigFile(mapArgs, mapMultiArgs);
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            wxMessageBox("Data directory not found. Check -datadir.", "Trinity");
            return false;
        }

        uiInterface.ThreadSafeMessageBox.connect(ThreadSafeMessageBox);
        uiInterface.ThreadSafeAskFee.connect(ThreadSafeAskFee);
        uiInterface.InitMessage.connect(InitMessage);
        uiInterface.Translate.connect(Translate);

        MainFrame *frame = new MainFrame();
        frame->Show(true);
        frame->Raise();
        frame->GetStatusBar()->SetStatusText("Starting Trinity core...");
        frame->Update();
        frame->Layout();

        threadGroup = new boost::thread_group;
        threadGroup->create_thread(boost::bind(&TrinityWxApp::InitializeCore, this, frame));

        return true;
    }

    int OnExit() override
    {
        if (threadGroup)
        {
            threadGroup->interrupt_all();
            threadGroup->join_all();
            delete threadGroup;
            threadGroup = NULL;
        }
        Shutdown();
        return 0;
    }

private:
    boost::thread_group *threadGroup = NULL;

    void InitializeCore(MainFrame *frame)
    {
        if (!AppInit2(*threadGroup))
        {
            wxCallAfter([frame]() {
                frame->GetStatusBar()->SetStatusText("Core startup failed.");
            });
            return;
        }

        wxCallAfter([frame]() {
            frame->GetStatusBar()->SetStatusText("Trinity core linked.");
            frame->Update();
            frame->Layout();
            frame->RefreshPanels();
        });
    }
};

wxIMPLEMENT_APP(TrinityWxApp);
