#ifndef PARAM_JSON
#define PARAM_JSON
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <regex>
#include "rapidjson/document.h"

enum IAA_VALUE_TYPE_INTER { TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_STRING };
typedef struct _Iaa_Param_Inter
{
    const char* name;
    IAA_VALUE_TYPE_INTER value_type;
    union {
        int i;
        float f;
        bool b;
        const char* s;
    } value;
}Iaa_Param_Inter;

template<typename T>
class ParamValueSub;

class ParamValueBase {
public:
    virtual ~ParamValueBase() {}
    virtual std::type_index type() const = 0;
    template<typename T>
    T get() const {
        if (type() != std::type_index(typeid(T))) {
            throw std::bad_cast();
        }
        return static_cast<const ParamValueSub<T>*>(this)->get();
    }
};

template<typename T>
class ParamValueSub : public ParamValueBase {
public:
    explicit ParamValueSub(const T& value) : value_(value) {}

    T get() const { return value_; }

    std::type_index type() const override {
        return std::type_index(typeid(T));
    }

private:
    T value_;
};

class ParamJson{
public:
    //static const char* kTypeNames[7];
    const char* json_path;
    bool invalid_command = false;
    std::unordered_map<std::string, std::string> param_list;
    std::unordered_map<std::string, std::shared_ptr<ParamValueBase>> default_param;
    rapidjson::Document doc;
    rapidjson::Document result_doc;

public:
    ParamJson(const char* path){
        json_path = path;
    }

    int GetParam(){
        std::ifstream file(json_path);
        std::stringstream buffer;
        if (!file.is_open()) {
            std::cerr << "无法打开 param.json 文件！" << std::endl;
            return -1;
        }
        buffer << file.rdbuf();
        std::string jsonStr = buffer.str();
        if (doc.Parse(jsonStr.c_str()).HasParseError()) {
            std::cerr << "param.json 解析失败！" << std::endl;
            return -1;
        }
        if (doc.IsArray()) {
            for(const auto &obj:doc.GetArray()){
                //加载param.json中的默认值到default_param
                if (obj.HasMember("default_value") && obj["default_value"].IsObject()){
                    for (rapidjson::Value::ConstMemberIterator itr = obj["default_value"].MemberBegin(); itr != obj["default_value"].MemberEnd(); ++itr){
                        if(itr->value.IsBool())default_param[itr->name.GetString()]=std::make_shared<ParamValueSub<bool>>(itr->value.GetBool());
                        else if(itr->value.IsNumber())default_param[itr->name.GetString()]=std::make_shared<ParamValueSub<float>>(itr->value.GetFloat());
                        else if(itr->value.IsString())default_param[itr->name.GetString()]=std::make_shared<ParamValueSub<const char *>>(itr->value.GetString());
                        //printf("key is %s, value type is %s\n", itr->name.GetString(), kTypeNames[itr->value.GetType()]);
                    }
                }
                //确认有那些被控参数,并读取prompt
                else{
                    for (rapidjson::Value::ConstMemberIterator itr = obj.MemberBegin(); itr != doc[0].MemberEnd(); ++itr){
                        param_list.insert(std::pair<std::string, std::string>(itr->name.GetString(), itr->value.GetString()));
                        //printf("Type of member %s is %s\n", itr->name.GetString(), kTypeNames[itr->value.GetType()]);
                    }
                }
            }
        }
        else{
            std::cerr << "param.json顶层不是Array,请重新编辑正确的param.json" << std::endl;
        }
        return 0;
    }

    //针对一些特别的无效指令,进行清理
    void command_clean(std::vector<Iaa_Param_Inter> &result, std::string input_str){
        for(auto& r:result){
            if (strcmp(r.name, "色板")==0){
                int pos;
                switch (r.value.i)
                {
                case 0:
                    if((pos=input_str.find("铁红"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 1:
                    if((pos=input_str.find("白热"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 2:
                    if((pos=input_str.find("红热"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 3:
                    if((pos=input_str.find("熔岩"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 4:
                    if((pos=input_str.find("高彩虹"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 5:
                    if((pos=input_str.find("彩虹"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                case 6:
                    if((pos=input_str.find("黑热"))==std::string::npos)
                    {
                        r.name = "无效指令";
                        r.value_type = TYPE_STRING;
                        r.value.s = "暂不支持该色板";
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    void fix_json(std::string &input_str){
        input_str = std::regex_replace(input_str, std::regex(R"(\}\s\}\s\])"), "}]" );
        input_str = std::regex_replace(input_str, std::regex(R"(\}\s\]\s\])"), "}]" );
        std::regex broken_missing_open(R"(,\s*(?!\{)\s*\"parameter\"\s*:\s*[^}]+\})");

        std::smatch match;
        std::string::const_iterator searchStart(input_str.cbegin());
        size_t offset = 0;

        while (std::regex_search(searchStart, input_str.cend(), match, broken_missing_open)) {
            size_t insert_pos = match.position(0) + offset;
            input_str.insert(insert_pos+1, "{");
            offset += 1;
            searchStart = input_str.cbegin() + insert_pos + match.length();
        }
    }

    int pars_control(std::string input_str, std::vector<Iaa_Param_Inter> &result, std::string user_str){
        //正确识别到了无效指令
        if (strcmp(input_str.c_str(), "暂不支持该操作")==0){
            Iaa_Param_Inter p;
            p.name = "无效指令";
            p.value_type = TYPE_STRING;
            p.value.s = default_param["无效指令"]->get<const char*>();
            result.push_back(p);
            return 0;
        }
        //将无效指令识别为了对话类型,或生成的json格式有误
        if (result_doc.Parse(input_str.c_str()).HasParseError()) {
            fix_json(input_str);
            if (result_doc.Parse(input_str.c_str()).HasParseError()){
                //std::cerr << "ai 指令解析失败！" << std::endl;
                Iaa_Param_Inter p;
                p.name = "无效指令";
                p.value_type = TYPE_STRING;
                p.value.s = default_param["无效指令"]->get<const char*>();
                result.push_back(p);
                return -1;
            }
            //else std::cout << "修复成功" << std::endl;
        }
        //识别正确的json格式和指令内容
        if (result_doc.IsArray()) {
            for(const auto &obj:result_doc.GetArray()){
                if (!obj.IsObject()) {
                    std::cerr << "json元素不是对象!" << std::endl;
                    continue;
                }
                if(obj.HasMember("parameter") && obj.HasMember("value")){
                    auto it = param_list.find(obj["parameter"].GetString());
                    //解析的parameter在默认参数列表中,否则为无效指令
                    if(it != param_list.end()){
                        Iaa_Param_Inter p;
                        p.name = it->first.c_str();
                        if(it->second==std::string("bool")){
                            p.value_type = TYPE_BOOL;
                            if (obj["value"].IsBool()){
                                p.value.b = obj["value"].GetBool();
                            }
                            else if (obj["value"].IsNumber()){
                                p.value.b = static_cast<bool>(obj["value"].GetFloat());
                            }
                            else {
                                p.value.b = static_cast<bool>(default_param[it->first]->get<bool>());
                            }
                        }
                        else if(it->second==std::string("int")){
                            p.value_type = TYPE_INT;
                            if (obj["value"].IsNumber()){
                                p.value.i = static_cast<int>(obj["value"].GetFloat());
                            }
                            else {
                                p.value.i = static_cast<int>(default_param[it->first]->get<float>());
                            }
                        }
                        else if(it->second==std::string("float")){
                            p.value_type = TYPE_FLOAT;
                            if (obj["value"].IsNumber()){
                                p.value.f = obj["value"].GetFloat();
                            }
                            else {
                                p.value.f = static_cast<float>(default_param[it->first]->get<float>());
                            }
                        }
                        else if(it->second==std::string("string")){
                            p.value_type = TYPE_STRING;
                            if(obj["value"].IsString()){
                                p.value.s = obj["value"].GetString();
                            }
                            else {
                                p.value.s = static_cast<const char*>(default_param[it->first]->get<const char *>());
                            }
                        }
                        result.push_back(p);
                    }
                    //解析的parameter不在默认参数列表中,添加"无效指令"字段
                    else{
                        if(!invalid_command){
                            Iaa_Param_Inter p;
                            p.name = "无效指令";
                            p.value_type = TYPE_STRING;
                            p.value.s = default_param["无效指令"]->get<const char*>();
                            result.push_back(p);
                        }
                        invalid_command = true;
                    }
                }            
            }
        command_clean(result, user_str);
        }
        return 0;
    }
};
//const char *ParamJson::kTypeNames[7] = { "none", "bool", "bool", "obj", "array", "string", "number" };

#endif // PARAM_JSON