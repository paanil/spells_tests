#include "rapidcheck.h"
#include "json_parser.h"

using namespace json;

#define INIT_INPUT() \
char input[64*1024]; int len = 0

#define APPEND_INPUT(fmt, ...) \
len += snprintf(input + len, sizeof(input) - len, fmt, ##__VA_ARGS__)

#define APPEND_Int(val) APPEND_INPUT("%d", val)
#define APPEND_Bool(val) APPEND_INPUT("%s", (val) ? "true" : "false")
#define APPEND_String(val) \
RC_PRE(val.find('"') == std::string::npos); \
APPEND_INPUT("\"%s\"", val.c_str())

#define CHECK(name, type, Type) \
rc::check(name, [](type val) { \
    INIT_INPUT(); \
    APPEND_##Type(val); \
    Value *result = Parse(input); \
    RC_ASSERT(result); \
    RC_ASSERT(Is##Type(result)); \
    RC_ASSERT(Get##Type(result) == val); \
    Free(result); \
})

#define APPEND_LIST(Type, vec) \
APPEND_INPUT("["); \
if (!vec.empty()) { \
    APPEND_##Type(vec[0]); \
    for (size_t index = 1; index < vec.size(); index++) { \
        APPEND_INPUT(","); \
        APPEND_##Type(vec[index]); \
    } \
} \
APPEND_INPUT("]")

#define CHECK_LIST_CONTENTS(list, type, Type, check_against) { \
    Value *item = GetFirstListItem(list); \
    for (const type &val : check_against) { \
        RC_ASSERT(item); \
        RC_ASSERT(Is##Type(item)); \
        RC_ASSERT(Get##Type(item) == val); \
        item = GetNextListItem(item); \
    } \
    RC_ASSERT(!item); \
}

#define CHECK_LIST(name, type, Type) \
rc::check(name, [](std::vector<type> vec) { \
    INIT_INPUT(); \
    APPEND_LIST(Type, vec); \
    Value *result = Parse(input); \
    RC_ASSERT(result); \
    RC_ASSERT(IsList(result)); \
    CHECK_LIST_CONTENTS(result, type, Type, vec); \
    Free(result); \
})

struct ObjectField
{
    enum Type
    {
        Int, Bool, String, List
    };
    
    std::string name;
    Type type;
    int int_value;
    bool bool_value;
    std::string string_value;
    std::vector<int> list_value;
};

std::ostream &operator<<(std::ostream &out, const ObjectField &field)
{
    out << '"' << field.name << "\":";
    switch (field.type)
    {
        case ObjectField::Int: out << field.int_value; break;
        case ObjectField::Bool: out << (field.bool_value ? "true" : "false"); break;
        case ObjectField::String: out << '"' << field.string_value << '"'; break;
        case ObjectField::List:
            out << '[';
            if (!field.list_value.empty())
            {
                out << field.list_value[0];
                for (size_t i = 1; i < field.list_value.size(); i++)
                    out << "," << field.list_value[i];
            }
            out << ']';
            break;
    }
    return out;
}

namespace rc {
    
    template<>
    struct Arbitrary<ObjectField>
    {
        static Gen<ObjectField> arbitrary()
        {
            return gen::build<ObjectField>(
                gen::set(&ObjectField::name,
                    gen::suchThat<std::string>([](std::string val)
                    { return val.find('"') == std::string::npos; })),
                gen::set(&ObjectField::type,
                    gen::element(
                        ObjectField::Int, ObjectField::Bool,
                        ObjectField::String, ObjectField::List)),
                gen::set(&ObjectField::int_value),
                gen::set(&ObjectField::bool_value),
                gen::set(&ObjectField::string_value,
                    gen::suchThat<std::string>([](std::string val)
                    { return val.find('"') == std::string::npos; })),
                gen::set(&ObjectField::list_value));
        }
    };
    
}

int main()
{
    CHECK("1.1 parsing integers", int, Int);
    CHECK("1.2 parsing booleans", bool, Bool);
    CHECK("1.3 parsing strings", std::string, String);
    
    CHECK_LIST("2.1 parsing lists of integers", int, Int);
    CHECK_LIST("2.2 parsing lists of booleans", bool, Bool);
    CHECK_LIST("2.3 parsing lists of strings", std::string, String);
    
    rc::check("2.4 parsing lists of lists",
        [](std::vector<std::vector<int> > vec) {
            INIT_INPUT();
            APPEND_INPUT("[");
            if (!vec.empty())
            {
                APPEND_LIST(Int, vec[0]);
                for (size_t i = 1; i < vec.size(); i++)
                {
                    APPEND_INPUT(",");
                    APPEND_LIST(Int, vec[i]);
                }
            }
            APPEND_INPUT("]");
            
            Value *result = Parse(input);
            RC_ASSERT(result);
            RC_ASSERT(IsList(result));
            Value *value = GetFirstListItem(result);
            for (const std::vector<int> &v : vec)
            {
                RC_ASSERT(value);
                RC_ASSERT(IsList(value));
                CHECK_LIST_CONTENTS(value, int, Int, v);
                value = GetNextListItem(value);
            }
            RC_ASSERT(!value);
            Free(result);
        }
    );
    
    typedef std::pair<std::string, int> IntField;
    
#define APPEND_INT_FIELD(field) \
    APPEND_String(field.first); \
    APPEND_INPUT(":"); \
    APPEND_Int(field.second)
    
    rc::check("3.1 parsing simple objects",
        [](std::vector<IntField> obj) {
            INIT_INPUT();
            APPEND_INPUT("{");
            if (!obj.empty())
            {
                APPEND_INT_FIELD(obj[0]);
                for (size_t i = 1; i < obj.size(); i++)
                {
                    APPEND_INPUT(",");
                    APPEND_INT_FIELD(obj[i]);
                }
            }
            APPEND_INPUT("}");
            
            Value *result = Parse(input);
            RC_ASSERT(result);
            RC_ASSERT(IsObject(result));
            for (const IntField &field : obj)
            {
                Value *value = GetFieldValue(result, field.first.c_str());
                RC_ASSERT(value);
                RC_ASSERT(IsInt(value));
                RC_ASSERT(GetInt(value) == field.second);
            }
            Free(result);
        }
    );
    
#define APPEND_FIELD(field) \
    APPEND_String(field.name); \
    APPEND_INPUT(":"); \
    switch (field.type) { \
        case ObjectField::Int: APPEND_Int(field.int_value); break; \
        case ObjectField::Bool: APPEND_Bool(field.bool_value); break; \
        case ObjectField::String: APPEND_String(field.string_value); break; \
        case ObjectField::List: APPEND_LIST(Int, field.list_value); break; \
    }
    
    rc::check("3.2 parsing complicated objects",
        [](std::vector<ObjectField> obj) {
            INIT_INPUT();
            APPEND_INPUT("{");
            if (!obj.empty())
            {
                APPEND_FIELD(obj[0]);
                for (size_t i = 1; i < obj.size(); i++)
                {
                    APPEND_INPUT(",");
                    APPEND_FIELD(obj[i]);
                }
            }
            APPEND_INPUT("}");
            
            Value *result = Parse(input);
            RC_ASSERT(result);
            RC_ASSERT(IsObject(result));
            for (const ObjectField &field : obj)
            {
                Value *value = GetFieldValue(result, field.name.c_str());
                RC_ASSERT(value);
                switch (field.type)
                { 
                    case ObjectField::Int: \
                        RC_ASSERT(IsInt(value)); \
                        RC_ASSERT(GetInt(value) == field.int_value); \
                        break;
                    case ObjectField::Bool: \
                        RC_ASSERT(IsBool(value)); \
                        RC_ASSERT(GetBool(value) == field.bool_value); \
                        break;
                    case ObjectField::String: \
                        RC_ASSERT(IsString(value)); \
                        RC_ASSERT(GetString(value) == field.string_value); \
                        break;
                    case ObjectField::List:
                        RC_ASSERT(IsList(value));
                        CHECK_LIST_CONTENTS(value, int, Int, field.list_value);
                        break; 
                }
            }
            Free(result);
        }
    );

    return 0;
}
