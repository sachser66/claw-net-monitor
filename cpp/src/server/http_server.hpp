#pragma once

#include <string>

void http_server_start(int port);
void http_server_publish_json(const std::string& json);
void http_server_stop();
