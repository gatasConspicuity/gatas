#include <stdio.h>

#include "../airconnect.hpp"
#include "ace/debug.hpp"

GATAS::PostConstruct AirConnect::postConstruct()
{
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(mutex, "AirConnect_mutex");
    return GATAS::PostConstruct::OK;
}

void AirConnect::start()
{
    getBus().subscribe(*this);
};

void AirConnect::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"connectedClients\":" << connectedClients.size();
    stream << ",\"toManyClients\":" << statistics.toManyClients;
    stream << ",\"tcpWriteErr\":" << statistics.tcpWriteErr;
    stream << ",\"newClientConnection\":" << statistics.newClientConnection;
    stream << "}";
}

/**
 * Receive dataport messages and send it to all clients
 */
void AirConnect::on_receive(const GATAS::DataPortMsg &msg)
{
    // Bus delivery can also enter from a lwIP/BTstack callback. Never wait
    // for this mutex while holding the shared CYW43/lwIP core lock.
    if (auto guard = SemaphoreGuard(0, mutex))
    {
        for (auto &connection : connectedClients)
        {
            connection.write(msg.sentence);
        }
    }
    else
    {
        statistics.tcpWriteErr++;
    }
}

void AirConnect::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void AirConnect::on_receive(const GATAS::Every5SecMsg &msg)
{
    (void)msg;
    if (auto guard = SemaphoreGuard(0, mutex))
    {
        for (auto it = connectedClients.begin(); it != connectedClients.end();)
        {
            if (it->isDisconnected())
            {
                // Wait for any final lwIP callback to finish before its
                // TcpClient callback argument is destroyed with the entry.
                it->tcpClient.stop();
                it = connectedClients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

err_t AirConnect::tcpAcceptHandler(struct tcp_pcb *pcb)
{
    // Returning an error lets lwIP dispose of the unadopted PCB. Waiting here
    // would deadlock a producer holding mutex while entering TcpClient::write.
    auto guard = SemaphoreGuard(0, mutex);
    if (!guard)
    {
        return ERR_MEM;
    }

    if (connectedClients.full())
    {
        statistics.toManyClients++;
        return ERR_MEM;
    }

    auto &client = connectedClients.emplace_back(pcb, this);
    client.write("AOK");
    statistics.newClientConnection++;
    return ERR_OK;
}

/**
 * Track wifi connects and disconnect
 */
void AirConnect::on_receive(const GATAS::WifiConnectionStateMsg &wcs)
{
    wifiConnected = wcs.wifiMode != GATAS::WifiMode::NC;

    if (wifiConnected)
    {
        tcpListener.start();
    }

    if (!wifiConnected)
    {
        tcpListener.stop();
    }
}
