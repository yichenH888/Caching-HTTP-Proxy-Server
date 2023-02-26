#include "proxy.hpp"

/**
 * create and setup a server listen socket
*/
void Proxy::initializeServerSocket() {
    server_socket = socket(host_info_list->ai_family,
                            host_info_list->ai_socktype,
                            host_info_list->ai_protocol);
    if (server_socket < 0) {
        cerr << "Server Initialization Failure: cannot create socket" << endl;
        exit(EXIT_FAILURE); 
    }
    int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        cerr << "Server Initialization Failure: cannot set socket option" << endl;
        exit(EXIT_FAILURE); 
    }
    if (bind(server_socket, host_info_list->ai_addr, host_info_list->ai_addrlen) == -1) { 
        cerr << "Server Initialization Failure: cannot bind socket" << endl; 
        exit(EXIT_FAILURE); 
    }   
    if (listen(server_socket, BACKLOG) == -1) { 
        cerr << "Server Initialization Failure: cannot listen socket" << endl; 
        exit(EXIT_FAILURE); 
    }
    freeaddrinfo(host_info_list);
}

/**
 * Parse client's ip address
 * @return client's ip address stored in string format
*/
string Proxy::parseClientIp(int client_socket) {
    socklen_t len;
    struct sockaddr_storage addr;
    char ipstr[INET_ADDRSTRLEN];
    len = sizeof addr;
    getpeername(client_socket, (struct sockaddr*)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    string ip(ipstr);
    return ip;
}

/**
 * listen to incomming connect client
 * update the ipToId map and start multi-threading
*/
void Proxy::serverListen() {
    struct sockaddr_in client_address;
    unsigned int client_address_len = sizeof(client_address);
    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if (client_socket < 0) {
            cerr << "Server Initialization Failure: cannot accept socket" << endl; 
            exit(EXIT_FAILURE); 
        }
        string clientIp = parseClientIp(client_socket);
        int newClientId = clientId;
        if (ipToIdMap.find(clientIp) != ipToIdMap.end()) {
            newClientId = ipToIdMap[clientIp];
        } else {
            ipToIdMap[clientIp] = newClientId;
            clientId++;
        }
        Client * client = new Client(clientIp, newClientId, client_socket);
        thread clientHandleThread([this, client](){
            Proxy::handler(client);
        });
        clientHandleThread.detach();
    }
}

/**
 * only public method for starting proxy service
*/
void Proxy::start() {
    serverListen();
}

//feel free to modify this function
void Proxy::handleRequest(Client * client) {
    try {
        // Read the request line from the client
        boost::beast::flat_buffer clientBuffer;
        http::request<http::string_body> request;
        http::read(client->getClientSocket(), clientBuffer, request);
        // Parse the request method
        string method = request.method_string().to_string();
        string requestTarget = string(request.target().data(),request.target().length());
        if (method == "CONNECT") {
            logger.logClientRequest(client, request);
            handleConnect(client, clientBuffer, requestTarget);
        }
        else if (method == "GET") {
            logger.logClientRequest(client, request);
            handleGet(client, clientBuffer, request);
        }
        else if (method == "POST") {

        }
        else {
            http::response<http::string_body> response{
                http::status::bad_request, request.version()};
            response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            response.keep_alive(false);
            response.body() = "Unsupported request method";
            response.prepare_payload();
            http::write(client->getClientSocket(), response);
        }
    }
    catch (exception &e){
        cout << "Error in handleRequest: " << e.what() << endl;
    }
}

/**
 * A handler for multi-threading. New thread does the thing in handler
*/
void Proxy::handler(Client* client) {
    // logger.logClientConnection(client);
    handleRequest(client);
    delete client;  
}

void Proxy::handleGet(Client * client, boost::beast::flat_buffer& clientBuffer, http::request<http::string_body> request) {
    // Parse the hostname and port from the GET request target
    string hostname;
    string port;
    string requestTarget = request.find(http::field::host)->value().to_string();
    parseHostnameAndPort(requestTarget, hostname, port, "get");
    cout << requestTarget << endl;

    // Resolve the hostname to an endpoint
    tcp::resolver resolver(client->getClientSocket().get_executor());
    tcp::resolver::query query(hostname, port);
    tcp::resolver::results_type endpoints = resolver.resolve(query);

    // Build a socket to the target server and connect to the target server
    tcp::socket remoteSocket(client->getClientSocket().get_executor());
    boost::asio::connect(remoteSocket, endpoints);

    //search in cache and find whether there is matched request
    if(cache.get(requestTarget)!=nullptr){//find in cache
        std::shared_ptr<pair<string, http::response<http::dynamic_body>>> target = cache.get(requestTarget);
        Response response(target->second);
        if(response.noCache || response.mustRevalidate){//旧response有no-cache和must revalidate(必须revalidate)
            http::write(remoteSocket, request);//revalidate, last-modified, etag
            //待写：收取新response
            //待写：判断是否有no-store/private, 若没有则存入cache
            //待写：将新response发送给client
        }
        else{//旧response没有no-cache
            //待写：收到旧response的时间+max-age+max-stale < 当前时间，则判断为过期，需要revalidate
                //待写：
                //假如需要revalidate
                    //发送request,获取新response, last-modified, etag
                    //看是否有no-store/private, 若没有则存入cache
                    //将新response发送给client
                //不需要revalidate
                    //从cache将reponse发给client
        }
    }
    else{//do not find in cache
        http::write(remoteSocket, request);//send request to target server
        //待写：收取response，分析后存入cache并转发, chunk???
    }
}

void Proxy::handleConnect(Client * client, boost::beast::flat_buffer& clientBuffer, string requestTarget) {
    // Parse the hostname and port from the CONNECT request target
    string hostname;
    string port;
    cout << requestTarget << endl;
    
    parseHostnameAndPort(requestTarget, hostname, port, "connect");
    // cout << "hostname: " << hostname << " port: " << port << endl;
    // Resolve the hostname to an endpoint
    tcp::resolver resolver(client->getClientSocket().get_executor());
    tcp::resolver::query query(hostname, port);
    tcp::resolver::results_type endpoints = resolver.resolve(query);

    // Build a socket to the target server and connect to the target server
    tcp::socket remoteSocket(client->getClientSocket().get_executor());
    boost::asio::connect(remoteSocket, endpoints);

    // Send 200 OK response to the client
    http::response<http::empty_body> response{boost::beast::http::status::ok, 11};
    // response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    // response.set(http::field::connection, "close");//let the client close the tunnel
    // response.keep_alive(false);
    http::write(client->getClientSocket(), response);

    // Forward data between the client and the remote server
    try {
        boost::beast::flat_buffer remoteBuffer;
        boost::system::error_code error;
        while (1) {
            // Read data from the client
            size_t clientLength = boost::asio::read(client->getClientSocket(), clientBuffer, boost::asio::transfer_at_least(1), error);
            if (error == boost::asio::error::eof) {
                // The client has closed the connection
                break;
            } else if (error) {
                // An error occurred
                throw boost::system::system_error(error);
            }

            // Forward the client data to the remote server
            boost::asio::write(remoteSocket, clientBuffer.data(), error);

            // Read data from the remote server
            size_t remoteLength = boost::asio::read(remoteSocket, remoteBuffer, boost::asio::transfer_at_least(1), error);

            if (error == boost::asio::error::eof) {
            // The remote server has closed the connection
            break;
            } 
            else if (error) {
            // An error occurred
            throw boost::system::system_error(error);
            }

            // Forward the remote server data to the client
            http::response<http::vector_body<char>> remoteResponse;
            remoteResponse.body().resize(remoteLength);
            memcpy(remoteResponse.body().data(), remoteBuffer.data().data(), remoteLength);
            remoteResponse.set(http::field::content_length, to_string(remoteLength));
            // remoteResponse.keep_alive(false);
            http::write(client->getClientSocket(), remoteResponse, error);
        }
        // Close the remote socket
        remoteSocket.shutdown(tcp::socket::shutdown_both, error);
        remoteSocket.close();
        logger.logTunnelClose(client);
    }
    catch (exception &e){
        cerr << "CONNECT request error: " << e.what() << endl;
        boost::system::error_code error;
        client->getClientSocket().shutdown(tcp::socket::shutdown_both, error);
        client->getClientSocket().close();
        remoteSocket.shutdown(tcp::socket::shutdown_both, error);
        remoteSocket.close();
    }
}

void Proxy::parseHostnameAndPort(const std::string& requestTarget, string &hostname, string &port, string method) {
    string::size_type pos = requestTarget.find(':');
    if (pos != string::npos) {
        hostname = requestTarget.substr(0, pos);
        port = requestTarget.substr(pos + 1);
    } else {
        hostname = requestTarget;
        if(method == "connect"){
            port = "443";
        }
        else if(method == "get"){
            port = "80";
        }
    }
}