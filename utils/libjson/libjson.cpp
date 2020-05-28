#include "libjson.hpp"
#include <assert.h>
#include <iostream>
#include <sstream>

//#define DEBUG_PARSER

using namespace Json;

Null Null::s_null_value_impl(nullptr);
Value *Value::s_null_value = &Null::s_null_value_impl;

#define ENUMARATE_STATES                   \
    __ENUMARATE_STATE(Invalid)             \
    __ENUMARATE_STATE(Initial)             \
    __ENUMARATE_STATE(Value)               \
    __ENUMARATE_STATE(String)              \
    __ENUMARATE_STATE(StringEscape)        \
    __ENUMARATE_STATE(StringUnicode)       \
    __ENUMARATE_STATE(NumberStart)         \
    __ENUMARATE_STATE(Number)              \
    __ENUMARATE_STATE(NumberFraction)      \
    __ENUMARATE_STATE(NumberExponentStart) \
    __ENUMARATE_STATE(NumberExponent)      \
    __ENUMARATE_STATE(NumberDone)          \
    __ENUMARATE_STATE(ObjectStart)         \
    __ENUMARATE_STATE(ObjectKey)           \
    __ENUMARATE_STATE(ObjectSeportator)    \
    __ENUMARATE_STATE(ObjectAdd)           \
    __ENUMARATE_STATE(ArrayStart)          \
    __ENUMARATE_STATE(ArrayValue)          \
    __ENUMARATE_STATE(ArrayNext)           \
    __ENUMARATE_STATE(Done)

enum class State
{
#define __ENUMARATE_STATE(x) x,
    ENUMARATE_STATES
#undef __ENUMARATE_STATE
};

#ifdef DEBUG_PARSER

static const char* state_to_string(State state)
{
    switch (state)
    {
#define __ENUMARATE_STATE(x) \
    case State::x:           \
        return #x;
        ENUMARATE_STATES
#undef __ENUMARATE_STATE
    }

    return "Unkown";
}

#endif

template <typename... Args>
bool is_one_of(uint32_t rune, Args... args)
{
    auto options = { args... };
    for (auto option : options)
    {
        if (rune == option)
            return true;
    }

    return false;
}

Document Document::parse(std::istream&& stream)
{
    Document doc;
    if (!stream.good())
        return doc;

    auto &allocator = doc.allocator();
    State state = State::Initial;
    std::vector<State> return_stack { State::Done };
    std::vector<Owner<Value>> value_stack;
    std::string buffer;

    // Pre allocate some memory to reduce allocations
    buffer.reserve(1024);
    return_stack.reserve(20);
    value_stack.reserve(20);

    size_t line = 1;
    size_t column = 1;
    auto emit_error = [&line, &column, &doc](std::string_view message) {
        Error error;
        error.line = line;
        error.column = column;
        error.message = message;
        doc.emit_error(std::move(error));
    };

    bool should_reconsume = false;
    uint32_t rune = 0;
    for (;;)
    {
        if (!should_reconsume)
        {
            column += 1;
            rune = stream.get();

            if (rune == '\n')
            {
                line += 1;
                column = 1;
            }
        }
        should_reconsume = false;

#ifdef DEBUG_PARSER
        std::cout << "State: " << state_to_string(state) << "\n";
#endif

        if (state == State::Done)
            break;

        if (rune == -1)
        {
            emit_error("Unexpected end of file");
            break;
        }

        switch (state)
        {
        default:
            assert(false);
            break;

        case State::Initial:
            if (isspace(rune))
                break;

            if (is_one_of(rune, '{', '[', '"', 't', 'f') || isdigit(rune))
            {
                state = State::Value;
                should_reconsume = true;
                break;
            }

            // TODO: Report parser error
            assert(false);
            break;

        case State::Value:
            if (isspace(rune))
                break;

            if (rune == '{')
            {
                value_stack.push_back(allocator.make<Object>());
                state = State::ObjectStart;
                break;
            }

            if (rune == '"')
            {
                state = State::String;
                break;
            }

            if (rune == '[')
            {
                value_stack.push_back(allocator.make<Array>());
                state = State::ArrayStart;
                break;
            }

            if (isdigit(rune) || rune == '-')
            {
                should_reconsume = true;
                state = State::NumberStart;
                break;
            }

            if (value_stack.back()->is<Array>() && rune == ']')
            {
                emit_error("Trainling ',' on end of array");
                return_stack.pop_back();
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            if (rune == '}' && value_stack.size() >= 2
                && value_stack[value_stack.size() - 1]->is<String>()
                && value_stack[value_stack.size() - 2]->is<Object>())
            {
                emit_error("Trainling ':' on end of object");
                value_stack.pop_back();
                return_stack.pop_back();
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            emit_error("Invalid value");
            break;

        case State::String:
            if (rune == '"')
            {
                value_stack.push_back(allocator.make_string_from_buffer(buffer));
                buffer.clear();

                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            if (rune == '\\')
            {
                state = State::StringEscape;
                break;
            }

            buffer += rune;
            break;

        case State::StringEscape:
            switch (rune)
            {
            case '"':
                buffer += '"';
                break;
            case '\\':
                buffer += '\\';
                break;
            case '/':
                buffer += '/';
                break;
            case 'b':
                buffer += '\b';
                break;
            case 'f':
                buffer += '\f';
                break;
            case 'n':
                buffer += '\n';
                break;
            case 'r':
                buffer += '\r';
                break;
            case 't':
                buffer += '\t';
                break;
            case 'u':
                break;
            default:
                assert(false);
                break;
            }

            if (rune == 'u')
            {
                state = State::StringUnicode;
                break;
            }

            state = State::String;
            break;

        case State::StringUnicode:
            // TODO: Unicode parsing
            assert(false);
            break;

        case State::NumberStart:
            // TODO: Should not accept any numbers starting with 0 other then 0, -0 and 0.x, ...
            if (rune == '-')
            {
                buffer += "-";
                state = State::Number;
                break;
            }

            should_reconsume = true;
            state = State::Number;
            break;

        case State::Number:
            if (isdigit(rune))
            {
                buffer += rune;
                break;
            }

            if (rune == '.')
            {
                buffer += rune;
                state = State::NumberFraction;
                break;
            }

            if (rune == 'E' || rune == 'e')
            {
                buffer += 'E';
                state = State::NumberExponentStart;
                break;
            }

            should_reconsume = true;
            state = State::NumberDone;
            break;

        case State::NumberFraction:
            if (isdigit(rune))
            {
                buffer += rune;
                break;
            }

            if (rune == 'E' || rune == 'e')
            {
                buffer += 'E';
                state = State::NumberExponentStart;
                break;
            }

            if (rune == '.')
            {
                emit_error("Multiple decmial places");
                break;
            }

            should_reconsume = true;
            state = State::NumberDone;
            break;

        case State::NumberExponentStart:
            if (rune == '-' || rune == '+')
            {
                buffer += rune;
                state = State::NumberExponent;
                break;
            }

            emit_error("Expected '+' or '-' before exponent");
            buffer += '+';
            state = State::NumberExponent;
            break;

        case State::NumberExponent:
            if (isdigit(rune))
            {
                buffer += rune;
                break;
            }

            should_reconsume = true;
            state = State::NumberDone;
            break;

        case State::NumberDone:
            value_stack.push_back(allocator.make<Number>(atof(buffer.c_str())));
            buffer.clear();

            should_reconsume = true;
            state = return_stack.back();
            return_stack.pop_back();
            break;

        case State::ObjectStart:
            if (isspace(rune))
                break;

            if (rune == '}')
            {
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            state = State::ObjectKey;
            should_reconsume = true;
            break;

        case State::ObjectKey:
            if (isspace(rune))
                break;

            if (rune == '"')
            {
                state = State::String;
                return_stack.push_back(State::ObjectSeportator);
                break;
            }

            if (rune == '}')
            {
                emit_error("Trailing ',' on end of object");
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            emit_error("Expected object key");
            state = State::ObjectKey;
            break;

        case State::ObjectSeportator:
            if (isspace(rune))
                break;

            if (rune == ':')
            {
                return_stack.push_back(State::ObjectAdd);
                state = State::Value;
                break;
            }

            emit_error("Expected ':' object seporator");
            should_reconsume = true;
            state = State::ObjectKey;
            value_stack.pop_back();
            break;

        case State::ObjectAdd:
        {
            if (isspace(rune))
                break;

            auto value = std::move(value_stack.back());
            value_stack.pop_back();
            auto key = std::move(value_stack.back());
            value_stack.pop_back();

            auto& object = value_stack.back();
            assert (object->is<Object>());
            object->add(key->to_string(), std::move(value));

            if (rune == ',')
            {
                state = State::ObjectKey;
                break;
            }

            if (rune == '}')
            {
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            emit_error("Expected ',' on end of object key-value pair");
            should_reconsume = true;
            state = State::ObjectKey;
            break;
        }

        case State::ArrayStart:
            if (isspace(rune))
                break;

            if (rune == ']')
            {
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            should_reconsume = true;
            state = State::ArrayValue;
            break;

        case State::ArrayValue:
            if (isspace(rune))
                break;

            should_reconsume = true;
            return_stack.push_back(State::ArrayNext);
            state = State::Value;
            break;

        case State::ArrayNext:
            if (isspace(rune))
                break;

            auto value = std::move(value_stack.back());
            value_stack.pop_back();

            auto& array = value_stack.back();
            assert (array->is<Array>());
            array->append(std::move(value));

            if (rune == ',')
            {
                state = State::ArrayValue;
                break;
            }

            if (rune == ']')
            {
                state = return_stack.back();
                return_stack.pop_back();
                break;
            }

            emit_error("Expected ',' between values in array");
            should_reconsume = true;
            state = State::ArrayValue;
            break;
        }
    }

    if (value_stack.size() != 1)
    {
        emit_error("There can only be one root value");
        return doc;
    }
    doc.set_root(std::move(value_stack[0]));
    return doc;
}

void Document::log_errors(std::ostream& stream)
{
    for (const auto& error : m_errors)
    {
        stream << "Error(" << error.line << ", " << error.column << ")";
        stream << ": " << error.message << "\n";
    }
}

static std::string print_indent(int indent)
{
    std::string out;
    for (int i = 0; i < indent; i++)
        out += "\t";
    return out;
}

static std::string serialize(const Value& value, int options, int indent = 0);
static std::string serialize_object(const Object& object, int options, int indent = 0)
{
    std::string out = "{";

    bool is_first = true;
    int new_indent = indent + ((int)options & (int)PrintOption::PrettyPrint ? 1 : 0);
    for (const auto& it : object)
    {
        if (!is_first)
            out += ", ";

        if ((int)options & (int)PrintOption::PrettyPrint)
            out += "\n";

        out += print_indent(new_indent);
        out += "\"" + it.first + "\": ";

        if ((int)options & (int)PrintOption::PrettyPrint
            && (it.second->is<Object>() || it.second->is<Array>()))
        {
            out += "\n" + print_indent(new_indent);
        }

        out += serialize(*it.second, options | (int)PrintOption::Serialize, new_indent);

        is_first = false;
    }

    if ((int)options & (int)PrintOption::PrettyPrint)
        out += "\n" + print_indent(indent);

    return out + "}";
}

static std::string serialize_array(const Array& array, int options, int indent = 0)
{
    std::string out = "[";

    bool is_first = true;
    int new_indent = indent + ((int)options & (int)PrintOption::PrettyPrint ? 1 : 0);
    for (const auto& item : array)
    {
        if (!is_first)
            out += ", ";

        if ((int)options & (int)PrintOption::PrettyPrint)
            out += "\n";

        out += print_indent(new_indent);
        out += serialize(*item, options | (int)PrintOption::Serialize, new_indent);

        is_first = false;
    }

    if ((int)options & (int)PrintOption::PrettyPrint)
        out += "\n" + print_indent(indent);

    return out + "]";
}

static std::string serialize_string(const String& string, int options)
{
    if (options & (int)PrintOption::Serialize)
        return "\"" + std::string(string.get_str()) + "\"";
    return std::string(string.get_str());
}

static std::string serialize_number(const Number& number)
{
    std::stringstream stream;
    stream << std::noshowpoint << number.to_double();
    return stream.str();
}

static std::string serialize_boolean(const Boolean& boolean)
{
    return boolean.to_bool() ? "true" : "false";
}

static std::string serialize(const Value& value, int options, int indent)
{
    if (value.is<String>())
        return serialize_string(static_cast<const String&>(value), options);

    else if (value.is<Number>())
        return serialize_number(static_cast<const Number&>(value));

    if (value.is<Boolean>())
        return serialize_boolean(static_cast<const Boolean&>(value));

    if (value.is<Object>())
        return serialize_object(static_cast<const Object&>(value), options, indent);

    if (value.is<Array>())
        return serialize_array(static_cast<const Array&>(value), options, indent);

    return "null";
}

std::string Value::to_string(PrintOption options) const
{
    return serialize(*this, (int)options);
}
std::string Null::to_string(PrintOption) const
{
    return "null";
}
std::string Object::to_string(PrintOption options) const
{
    return serialize_object(*this, (int)options);
}
std::string Array::to_string(PrintOption options) const
{
    return serialize_array(*this, (int)options);
}
std::string String::to_string(PrintOption options) const
{
    return serialize_string(*this, (int)options);
}
std::string Number::to_string(PrintOption) const
{
    return serialize_number(*this);
}
std::string Boolean::to_string(PrintOption) const
{
    return serialize_boolean(*this);
}

void Object::add(Allocator &allocator, const std::string& name, const std::string str)
{
    m_data[name] = allocator.make_string_from_buffer(str);
}

void Object::add(Allocator &allocator, const std::string& name, const char* str)
{
    m_data[name] = allocator.make_string_from_buffer(std::string(str));
}

void Object::add(Allocator &allocator, const std::string& name, double number)
{
    m_data[name] = allocator.make<Number>(number);
}

void Object::add(Allocator &allocator, const std::string& name, bool boolean)
{
    m_data[name] = allocator.make<Boolean>(boolean);
}

std::ostream& Json::operator<<(std::ostream& out, const Value &value)
{
    out << value.to_string();
    return out;
}

std::ostream& Json::operator<<(std::ostream& out, Owner<Value> &value)
{
    if (!value)
    {
        out << "<Null value>";
        return out;
    }

    out << *value;
    return out;
}
