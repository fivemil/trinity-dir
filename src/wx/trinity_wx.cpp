// Copyright (c) 2026 Trinity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/valnum.h>

#include <curl/curl.h>

#include "json/json_spirit_reader.h"
#include "json/json_spirit_writer.h"
#include "json/json_spirit_utils.h"

using namespace json_spirit;

namespace {
size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    if (!userdata)
        return 0;
    std::string *response = static_cast<std::string *>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string JsonValueToString(const Value &value)
{
    if (value.type() == str_type)
        return value.get_str();
    return write_string(value, false);
}
} // namespace

class RpcClient
{
public:
    RpcClient() : port(6420), useSSL(false) {}

    void Configure(const std::string &hostIn, int portIn, const std::string &userIn,
                   const std::string &passwordIn, bool useSSLIn)
    {
        host = hostIn;
        port = portIn;
        user = userIn;
        password = passwordIn;
        useSSL = useSSLIn;
    }

    bool IsConfigured() const
    {
        return !host.empty() && !user.empty() && !password.empty();
    }

    bool Call(const std::string &method, const Array &params, Value &result, std::string &error) const
    {
        if (!IsConfigured())
        {
            error = "RPC credentials are not set.";
            return false;
        }

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            error = "Unable to initialize RPC client.";
            return false;
        }

        std::string url = std::string(useSSL ? "https://" : "http://") + host + ":" + wxString::Format("%d", port).ToStdString();
        Object request;
        request.push_back(Pair("method", method));
        request.push_back(Pair("params", params));
        request.push_back(Pair("id", 1));
        std::string payload = write_string(Value(request), false);

        std::string response;
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (user + ":" + password).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            error = curl_easy_strerror(res);
            return false;
        }

        Value replyValue;
        if (!read_string(response, replyValue))
        {
            error = "Unable to parse RPC response.";
            return false;
        }

        if (replyValue.type() != obj_type)
        {
            error = "Unexpected RPC response.";
            return false;
        }

        Object reply = replyValue.get_obj();
        Value errorVal = find_value(reply, "error");
        if (errorVal.type() != null_type)
        {
            if (errorVal.type() == obj_type)
            {
                Object errorObj = errorVal.get_obj();
                Value messageVal = find_value(errorObj, "message");
                if (messageVal.type() == str_type)
                    error = messageVal.get_str();
                else
                    error = JsonValueToString(errorVal);
            }
            else
                error = JsonValueToString(errorVal);
            return false;
        }

        result = find_value(reply, "result");
        return true;
    }

private:
    std::string host;
    int port;
    std::string user;
    std::string password;
    bool useSSL;
};

class OverviewPanel : public wxPanel
{
public:
    OverviewPanel(wxWindow *parent, RpcClient &rpcClientIn, wxStatusBar *statusBarIn)
        : wxPanel(parent), rpcClient(rpcClientIn), statusBar(statusBarIn)
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
        Value infoValue;
        std::string error;
        if (!rpcClient.Call("getinfo", Array(), infoValue, error))
        {
            ReportError("getinfo", error);
            return;
        }

        Object infoObj = infoValue.get_obj();
        clientVersion->SetLabel(JsonValueToString(find_value(infoObj, "version")));
        protocolVersion->SetLabel(JsonValueToString(find_value(infoObj, "protocolversion")));
        balance->SetLabel(JsonValueToString(find_value(infoObj, "balance")));
        blocks->SetLabel(JsonValueToString(find_value(infoObj, "blocks")));
        connections->SetLabel(JsonValueToString(find_value(infoObj, "connections")));
        powAlgo->SetLabel(JsonValueToString(find_value(infoObj, "pow_algo")));
        difficulty->SetLabel(JsonValueToString(find_value(infoObj, "difficulty")));

        Value miningValue;
        if (rpcClient.Call("getmininginfo", Array(), miningValue, error))
        {
            Object miningObj = miningValue.get_obj();
            hashrate->SetLabel(JsonValueToString(find_value(miningObj, "hashespersec")) + " H/s");
        }
    }

private:
    void AddRow(wxFlexGridSizer *grid, const wxString &labelText, wxStaticText *&valueLabel)
    {
        wxStaticText *label = new wxStaticText(this, wxID_ANY, labelText);
        valueLabel = new wxStaticText(this, wxID_ANY, "-");
        grid->Add(label, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        grid->Add(valueLabel, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    }

    void ReportError(const std::string &context, const std::string &error)
    {
        statusBar->SetStatusText(wxString::Format("RPC error (%s): %s", context, error));
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    RpcClient &rpcClient;
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
    WalletPanel(wxWindow *parent, RpcClient &rpcClientIn, wxStatusBar *statusBarIn)
        : wxPanel(parent), rpcClient(rpcClientIn), statusBar(statusBarIn)
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
        std::string error;
        Value balanceValue;
        if (rpcClient.Call("getbalance", Array(), balanceValue, error))
            balanceText->SetLabel(JsonValueToString(balanceValue));
        else
            ReportError("getbalance", error);

        RefreshTransactions();
        RefreshReceived();
    }

private:
    void ReportError(const std::string &context, const std::string &error)
    {
        statusBar->SetStatusText(wxString::Format("RPC error (%s): %s", context, error));
    }

    void RefreshTransactions()
    {
        std::string error;
        Array params;
        params.push_back(Value("*"));
        params.push_back(Value(10));
        params.push_back(Value(0));
        Value txValue;
        if (!rpcClient.Call("listtransactions", params, txValue, error))
        {
            ReportError("listtransactions", error);
            return;
        }

        transactionsList->DeleteAllItems();
        Array txArray = txValue.get_array();
        long index = 0;
        for (Array::const_iterator it = txArray.begin(); it != txArray.end(); ++it)
        {
            if (it->type() != obj_type)
                continue;
            Object txObj = it->get_obj();
            long itemIndex = transactionsList->InsertItem(index, FormatTimestamp(find_value(txObj, "time")));
            transactionsList->SetItem(itemIndex, 1, JsonValueToString(find_value(txObj, "category")));
            transactionsList->SetItem(itemIndex, 2, JsonValueToString(find_value(txObj, "amount")));
            transactionsList->SetItem(itemIndex, 3, JsonValueToString(find_value(txObj, "address")));
            transactionsList->SetItem(itemIndex, 4, JsonValueToString(find_value(txObj, "txid")));
            ++index;
        }
    }

    void RefreshReceived()
    {
        std::string error;
        Array params;
        params.push_back(Value(1));
        params.push_back(Value(false));
        Value receivedValue;
        if (!rpcClient.Call("listreceivedbyaddress", params, receivedValue, error))
        {
            ReportError("listreceivedbyaddress", error);
            return;
        }

        receivedList->DeleteAllItems();
        Array receivedArray = receivedValue.get_array();
        long index = 0;
        for (Array::const_iterator it = receivedArray.begin(); it != receivedArray.end(); ++it)
        {
            if (it->type() != obj_type)
                continue;
            Object obj = it->get_obj();
            long itemIndex = receivedList->InsertItem(index, JsonValueToString(find_value(obj, "address")));
            receivedList->SetItem(itemIndex, 1, JsonValueToString(find_value(obj, "account")));
            receivedList->SetItem(itemIndex, 2, JsonValueToString(find_value(obj, "amount")));
            receivedList->SetItem(itemIndex, 3, JsonValueToString(find_value(obj, "confirmations")));
            ++index;
        }
    }

    wxString FormatTimestamp(const Value &value)
    {
        if (value.type() == int_type)
        {
            wxDateTime dt = wxDateTime::FromTimeT(value.get_int());
            return dt.FormatISOCombined(' ');
        }
        if (value.type() == int64_type)
        {
            wxDateTime dt = wxDateTime::FromTimeT(static_cast<time_t>(value.get_int64()));
            return dt.FormatISOCombined(' ');
        }
        return "-";
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    void OnNewAddress(wxCommandEvent &)
    {
        std::string error;
        Value result;
        if (rpcClient.Call("getnewaddress", Array(), result, error))
        {
            newAddress->SetValue(JsonValueToString(result));
            statusBar->SetStatusText("Generated new receive address.");
        }
        else
            ReportError("getnewaddress", error);
    }

    void OnSend(wxCommandEvent &)
    {
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

        Array params;
        params.push_back(Value(address));
        params.push_back(Value(amount));
        Value result;
        std::string error;
        if (rpcClient.Call("sendtoaddress", params, result, error))
        {
            statusBar->SetStatusText("Transaction sent: " + JsonValueToString(result));
            Refresh();
        }
        else
            ReportError("sendtoaddress", error);
    }

    RpcClient &rpcClient;
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
    ExplorerPanel(wxWindow *parent, RpcClient &rpcClientIn, wxStatusBar *statusBarIn)
        : wxPanel(parent), rpcClient(rpcClientIn), statusBar(statusBarIn)
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
    void ReportError(const std::string &context, const std::string &error)
    {
        statusBar->SetStatusText(wxString::Format("RPC error (%s): %s", context, error));
    }

    void OnFetchHeight(wxCommandEvent &)
    {
        Array params;
        params.push_back(Value(blockHeight->GetValue()));
        std::string error;
        Value hashValue;
        if (!rpcClient.Call("getblockhash", params, hashValue, error))
        {
            ReportError("getblockhash", error);
            return;
        }
        blockHash->SetValue(JsonValueToString(hashValue));
        FetchBlock(JsonValueToString(hashValue));
    }

    void OnFetchHash(wxCommandEvent &)
    {
        std::string hash = blockHash->GetValue().ToStdString();
        if (hash.empty())
        {
            statusBar->SetStatusText("Enter a block hash.");
            return;
        }
        FetchBlock(hash);
    }

    void FetchBlock(const std::string &hash)
    {
        Array params;
        params.push_back(Value(hash));
        Value blockValue;
        std::string error;
        if (!rpcClient.Call("getblock", params, blockValue, error))
        {
            ReportError("getblock", error);
            return;
        }

        Object blockObj = blockValue.get_obj();
        std::ostringstream summary;
        summary << "Block " << JsonValueToString(find_value(blockObj, "height")) << "\n";
        summary << "Hash: " << JsonValueToString(find_value(blockObj, "hash")) << "\n";
        summary << "Confirmations: " << JsonValueToString(find_value(blockObj, "confirmations")) << "\n";
        summary << "Time: " << JsonValueToString(find_value(blockObj, "time")) << "\n";
        Value txs = find_value(blockObj, "tx");
        if (txs.type() == array_type)
            summary << "Transactions: " << txs.get_array().size() << "\n";
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

        Array params;
        params.push_back(Value(id));
        Value rawValue;
        std::string error;
        if (!rpcClient.Call("getrawtransaction", params, rawValue, error))
        {
            ReportError("getrawtransaction", error);
            return;
        }

        Array decodeParams;
        decodeParams.push_back(rawValue);
        Value decodedValue;
        if (!rpcClient.Call("decoderawtransaction", decodeParams, decodedValue, error))
        {
            ReportError("decoderawtransaction", error);
            return;
        }

        results->SetValue(write_string(decodedValue, true));
    }

    RpcClient &rpcClient;
    wxStatusBar *statusBar;
    wxSpinCtrl *blockHeight;
    wxTextCtrl *blockHash;
    wxTextCtrl *txId;
    wxTextCtrl *results;
};

class AiPanel : public wxPanel
{
public:
    AiPanel(wxWindow *parent, RpcClient &rpcClientIn, wxStatusBar *statusBarIn)
        : wxPanel(parent), rpcClient(rpcClientIn), statusBar(statusBarIn)
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
        Value miningValue;
        std::string error;
        if (!rpcClient.Call("getmininginfo", Array(), miningValue, error))
        {
            ReportError("getmininginfo", error);
            return;
        }

        Object miningObj = miningValue.get_obj();
        powAlgo->SetLabel(JsonValueToString(find_value(miningObj, "pow_algo")));
        difficulty->SetLabel(JsonValueToString(find_value(miningObj, "difficulty")));
        hashrate->SetLabel(JsonValueToString(find_value(miningObj, "hashespersec")) + " H/s");

        double difficultyVal = 0.0;
        double hashrateVal = 0.0;
        Value diffValue = find_value(miningObj, "difficulty");
        if (diffValue.type() == real_type || diffValue.type() == int_type)
            difficultyVal = diffValue.get_real();
        Value hashValue = find_value(miningObj, "hashespersec");
        if (hashValue.type() == real_type || hashValue.type() == int_type)
            hashrateVal = hashValue.get_real();
        double score = difficultyVal * hashrateVal;
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

    void ReportError(const std::string &context, const std::string &error)
    {
        statusBar->SetStatusText(wxString::Format("RPC error (%s): %s", context, error));
    }

    void OnRefresh(wxCommandEvent &)
    {
        Refresh();
    }

    RpcClient &rpcClient;
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
        statusBar->SetStatusText("Configure RPC access to begin.");

        wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer *connectionSizer = new wxStaticBoxSizer(wxHORIZONTAL, this, "RPC Connection");
        hostInput = new wxTextCtrl(this, wxID_ANY, "127.0.0.1");
        portInput = new wxSpinCtrl(this, wxID_ANY);
        portInput->SetRange(1, 65535);
        portInput->SetValue(6420);
        userInput = new wxTextCtrl(this, wxID_ANY);
        passwordInput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        sslCheckbox = new wxCheckBox(this, wxID_ANY, "Use SSL");
        wxButton *connectButton = new wxButton(this, wxID_ANY, "Connect");
        connectButton->Bind(wxEVT_BUTTON, &MainFrame::OnConnect, this);

        connectionSizer->Add(new wxStaticText(this, wxID_ANY, "Host:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        connectionSizer->Add(hostInput, 0, wxRIGHT, 10);
        connectionSizer->Add(new wxStaticText(this, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        connectionSizer->Add(portInput, 0, wxRIGHT, 10);
        connectionSizer->Add(new wxStaticText(this, wxID_ANY, "User:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        connectionSizer->Add(userInput, 0, wxRIGHT, 10);
        connectionSizer->Add(new wxStaticText(this, wxID_ANY, "Password:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        connectionSizer->Add(passwordInput, 0, wxRIGHT, 10);
        connectionSizer->Add(sslCheckbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        connectionSizer->Add(connectButton, 0);

        notebook = new wxNotebook(this, wxID_ANY);
        overviewPanel = new OverviewPanel(notebook, rpcClient, statusBar);
        walletPanel = new WalletPanel(notebook, rpcClient, statusBar);
        explorerPanel = new ExplorerPanel(notebook, rpcClient, statusBar);
        aiPanel = new AiPanel(notebook, rpcClient, statusBar);

        notebook->AddPage(overviewPanel, "Overview", true);
        notebook->AddPage(walletPanel, "Wallet");
        notebook->AddPage(explorerPanel, "Explorer");
        notebook->AddPage(aiPanel, "AI Power");

        mainSizer->Add(connectionSizer, 0, wxALL | wxEXPAND, 12);
        mainSizer->Add(notebook, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        SetSizer(mainSizer);
    }

private:
    void OnConnect(wxCommandEvent &)
    {
        rpcClient.Configure(
            hostInput->GetValue().ToStdString(),
            portInput->GetValue(),
            userInput->GetValue().ToStdString(),
            passwordInput->GetValue().ToStdString(),
            sslCheckbox->IsChecked());

        if (!rpcClient.IsConfigured())
        {
            statusBar->SetStatusText("Provide RPC username and password.");
            return;
        }

        statusBar->SetStatusText("Connecting to Trinity RPC...");
        overviewPanel->Refresh();
        walletPanel->Refresh();
        explorerPanel->Refresh();
        aiPanel->Refresh();
        statusBar->SetStatusText("RPC connected.");
    }

    RpcClient rpcClient;
    wxStatusBar *statusBar;
    wxNotebook *notebook;
    OverviewPanel *overviewPanel;
    WalletPanel *walletPanel;
    ExplorerPanel *explorerPanel;
    AiPanel *aiPanel;
    wxTextCtrl *hostInput;
    wxSpinCtrl *portInput;
    wxTextCtrl *userInput;
    wxTextCtrl *passwordInput;
    wxCheckBox *sslCheckbox;
};

class TrinityWxApp : public wxApp
{
public:
    bool OnInit() override
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        MainFrame *frame = new MainFrame();
        frame->Show(true);
        return true;
    }

    int OnExit() override
    {
        curl_global_cleanup();
        return 0;
    }
};

wxIMPLEMENT_APP(TrinityWxApp);
