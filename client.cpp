#include "client.hpp"

/**
 * Log client id and ip address into target log file.
*/
void Client::logConnectMessage() {
    lock_guard<mutex> lock(logMutexLock);
    ofstream logfile("./proxy.log", ios::app); // LOG_FILE is defined in constant.hpp
    if (logfile.is_open()) {
        logfile << "Client connect to the server. Client ID: " << clientId << " Client IP: " << ip  << endl;
        logfile.close();
    } else {
        cerr << "Error: Could not open log file for writing." << endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * Getter for clientSocket
 * @return 
*/
boost::asio::ip::tcp::socket& Client::getClientSocket() {
    return clientSocket;
}

