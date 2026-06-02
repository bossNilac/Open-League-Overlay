#include "json_parser.h"

#include <json/json.h>
#include <json/value.h>
#include <memory>

bool parseApiJson(const std::string& json, Json::Value& root, std::string& error) {
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const char* begin = json.data();
    const char* end = begin + json.size();
    return reader->parse(begin, end, &root, &error);
}

Json::Value parseApiJson(std::string json) {
    Json::Value root;
    std::string error;
    parseApiJson(json, root, error);
    return root;
}
