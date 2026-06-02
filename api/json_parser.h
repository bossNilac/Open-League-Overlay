#ifndef LOL_OVERLAY_JSON_PARSER_H
#define LOL_OVERLAY_JSON_PARSER_H
#include <json/json.h>
#include <string>

bool parseApiJson(const std::string& json, Json::Value& root, std::string& error);
Json::Value parseApiJson(std::string json);

#endif //LOL_OVERLAY_JSON_PARSER_H
