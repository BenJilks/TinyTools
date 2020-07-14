#pragma once
#include <iostream>
#include <optional>
#include <vector>

class Lexer
{
public:
	struct Token
	{
		enum Type
		{
			Identifier,
			Number,
			Keyword,
			Symbol,
			StringType,
		};
		
		std::string data;
		Type type;
	};

	Lexer(std::istream &in) 
		: m_in(in) {}

	void add_keyword(const std::string&);
	void add_symbol(const std::string&);
	void add_string_type(char dilim, bool single_char = false);
	
	std::optional<Token> next();

private:
	enum class State
	{
		Default,
		CheckSymbol,
		Name,
		Number,
		StringType,
		EscapeCode,
	};

	struct StringType
	{
		char dilim;
		bool single_char;
	};

	Token::Type keyword_type(const std::string &buffer) const;

	State m_state { State::Default };
	char m_curr_char { 0 };
	bool m_should_reconsume { false };
	std::istream &m_in;
	
	std::vector<std::string> m_keywords;
	std::vector<std::string> m_symbols;
	std::vector<StringType> m_string_types;

};
