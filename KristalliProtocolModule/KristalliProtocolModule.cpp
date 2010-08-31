/** @file
    @author LudoCraft Oy

    Copyright 2009 LudoCraft Oy.
    All rights reserved.

    @brief
*/
#include "StableHeaders.h"

#include "DebugOperatorNew.h"

#include "KristalliProtocolModule.h"
#include "KristalliProtocolModuleEvents.h"
#include "Profiler.h"
#include "EventManager.h"

namespace KristalliProtocol
{

namespace
{
    const std::string moduleName("KristalliProtocolModule");

    const struct
    {
        SocketTransportLayer transport;
        int portNumber;
    } destinationPorts[] = 
    {
        { SocketOverUDP, 2345 }, // The default Kristalli over UDP port.

        { SocketOverTCP, 2345 }, // The default Kristalli over TCP port.
/*
        { SocketOverUDP, 123 }, // Network Time Protocol.

        { SocketOverTCP, 80 }, // HTTP.
        { SocketOverTCP, 443 }, // HTTPS.
        { SocketOverTCP, 20 }, // FTP Data.
        { SocketOverTCP, 21 }, // FTP Control.
        { SocketOverTCP, 22 }, // SSH.
        { SocketOverTCP, 23 }, // TELNET.
        { SocketOverUDP, 25 }, // SMTP. (Microsoft)
        { SocketOverTCP, 25 }, // SMTP.
        { SocketOverTCP, 110 }, // POP3 Server listen port.
        { SocketOverTCP, 995 }, // POP3 over SSL.
        { SocketOverTCP, 109 }, // POP2.
        { SocketOverTCP, 6667 }, // IRC.

        // For more info on the following windows ports, see: http://support.microsoft.com/kb/832017

        { SocketOverTCP, 135 }, // Windows RPC.
        { SocketOverUDP, 137 }, // Windows Cluster Administrator. / NetBIOS Name Resolution.
        { SocketOverUDP, 138 }, // Windows NetBIOS Datagram Service.
        { SocketOverTCP, 139 }, // Windows NetBIOS Session Service.

        { SocketOverUDP, 389 }, // Windows LDAP Server.
        { SocketOverTCP, 389 }, // Windows LDAP Server.

        { SocketOverTCP, 445 }, // Windows SMB.

        { SocketOverTCP, 5722 }, // Windows RPC.

        { SocketOverTCP, 993 }, // IMAP over SSL.

//        { SocketOverTCP, 1433 }, // SQL over TCP.
//        { SocketOverUDP, 1434 }, // SQL over UDP.

        { SocketOverUDP, 53 }, // DNS.
        { SocketOverTCP, 53 }, // DNS. Microsoft states it uses TCP 53 for DNS as well.
        { SocketOverUDP, 161 }, // SNMP agent port.
        { SocketOverUDP, 162 }, // SNMP manager port.
        { SocketOverUDP, 520 }, // RIP.
        { SocketOverUDP, 67 }, // DHCP client->server.
        { SocketOverUDP, 68 }, // DHCP server->client.
*/
    };

    /// The number of different port choices to try from the list.
    const int cNumPortChoices = sizeof(destinationPorts) / sizeof(destinationPorts[0]);

}

KristalliProtocolModule::KristalliProtocolModule()
:ModuleInterface(NameStatic())
, serverConnection(0),
nextPortAttempt(0)
{
}

KristalliProtocolModule::~KristalliProtocolModule()
{
    Disconnect();
}

void KristalliProtocolModule::Load()
{
}

void KristalliProtocolModule::Unload()
{
    Disconnect();
}

void KristalliProtocolModule::PreInitialize()
{
}

void KristalliProtocolModule::Initialize()
{
    Foundation::EventManagerPtr event_manager = framework_->GetEventManager();

    networkEventCategory = event_manager->RegisterEventCategory("Kristalli");
    event_manager->RegisterEvent(networkEventCategory, Events::NETMESSAGE_IN, "NetMessageIn");
}

void KristalliProtocolModule::PostInitialize()
{
}

void KristalliProtocolModule::Uninitialize()
{
    Disconnect();
}

void KristalliProtocolModule::Update(f64 frametime)
{
    // Pulls all new inbound network messages and calls the message handler we've registered
    // for each of them.
    if (serverConnection)
        serverConnection->ProcessMessages();

    if ((!serverConnection || serverConnection->GetConnectionState() == ConnectionClosed || serverConnection->GetConnectionState() == ConnectionPending) && serverIp.length() != 0)
    {
        const int cReconnectTimeout = 5 * 1000.f;
        if (reconnectTimer.Test())
        {
            nextPortAttempt = (nextPortAttempt + 1) % cNumPortChoices;
            PerformConnection();
        }
        else if (!reconnectTimer.Enabled())
            reconnectTimer.StartMSecs(cReconnectTimeout);
    }
/*
    if (connectionPending && serverConnection && serverConnection->GetConnectionState() == ConnectionOK)
    {
        connection->SetDatagramInFlowRatePerSecond(200);
        
        // Kristalli connection is established, now send a login message
        LogInfo("KristalliServer connection established, sending login message");
        if (!worldStream_)
        {
            LogError("No worldstream, cannot proceed with KristalliServer login");
            return;
        }
        
        ProtocolUtilities::ClientParameters& clientParams = worldStream_->GetInfo();
        
        MsgLogin msg;
        for (unsigned i = 0; i < RexUUID::cSizeBytes; ++i)
            msg.userUUID[i] = clientParams.agentID.data[i];
        msg.userName = StringToBuffer(worldStream_->GetUsername());
        connection->Send(msg);
    }
*/
    RESETPROFILER;
}

const std::string &KristalliProtocolModule::NameStatic()
{
    return moduleName;
}

void KristalliProtocolModule::Connect(const char *ip, unsigned short port)
{
    serverIp = ip;

    if (Connected() && serverConnection && serverConnection->GetEndPoint().ToString() != serverIp)
        Disconnect();

    if (!Connected())
        PerformConnection(); // Start performing a connection attempt to the desired address, using the 
}

void KristalliProtocolModule::PerformConnection()
{
    if (Connected() && serverConnection)
    {
        network.CloseMessageConnection(serverConnection);
        serverConnection = 0;
    }

    // Connect to the server.
    serverConnection = network.Connect(serverIp.c_str(), destinationPorts[nextPortAttempt].portNumber, destinationPorts[nextPortAttempt].transport, this);
    if (!serverConnection)
    {
        std::cout << "Unable to connect to " << serverIp << ":" << destinationPorts[nextPortAttempt].portNumber << std::endl;
        return;
    }
}

void KristalliProtocolModule::Disconnect()
{
    if (serverConnection)
    {
        network.CloseMessageConnection(serverConnection);
        serverConnection = 0;
    }
    nextPortAttempt = 0;

    // Clear the remembered destination server ip address so that the automatic connection timer will not try to reconnect.
    serverIp = "";
}

void KristalliProtocolModule::HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes)
{
    assert(source);
    assert(data);

    try
    {
        Events::KristalliNetMessageIn msg(source, id, data, numBytes);

        framework_->GetEventManager()->SendEvent(networkEventCategory, Events::NETMESSAGE_IN, &msg);
    } catch(std::exception &e)
    {
        std::cerr << "KristalliProtocolModule: Exception \"" << e.what() << "\" thrown when handling network message id " << id << " size " << (int)numBytes << " from client " 
            << source->ToString() << std::endl;
    }
}

bool KristalliProtocolModule::HandleEvent(event_category_id_t category_id, event_id_t event_id, Foundation::EventDataInterface* data)
{
    return false;
}

} // ~KristalliProtocolModule namespace

extern "C" void POCO_LIBRARY_API SetProfiler(Foundation::Profiler *profiler);
void SetProfiler(Foundation::Profiler *profiler)
{
    Foundation::ProfilerSection::SetProfiler(profiler);
}

using namespace KristalliProtocol;

POCO_BEGIN_MANIFEST(Foundation::ModuleInterface)
   POCO_EXPORT_CLASS(KristalliProtocolModule)
POCO_END_MANIFEST