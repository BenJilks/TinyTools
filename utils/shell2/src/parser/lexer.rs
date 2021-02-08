use crate::parser::token::{Token, TokenType, UnexpectedError, Error};
use std::io::{Read, Bytes};
use std::iter::Peekable;

enum State
{
    Initial,
    Name,
    And,
    Pipe,
    Variable,
    Assignement,
    String,
}

pub struct Lexer<S: Read>
{
    source: Peekable<Bytes<S>>,

    state: State,
    buffer: String,
    value_buffer: String,
}

fn is_name_char(c: char) -> bool
{
    c.is_alphanumeric() || ['_', '-', '/', '.', '~'].contains(&c)
}

impl<S: Read> Lexer<S>
{
    
    pub fn new(source: S) -> Self
    {
        Self 
        {
            source: source.bytes().peekable(),

            state: State::Initial,
            buffer: String::new(),
            value_buffer: String::new(),
        }
    }

    fn single_char(&mut self, token_type: TokenType, name: &str) -> Option<Result<Token, Error>>
    {
        self.source.next();
        Token::new(token_type, name)
    }

    fn handle_char(&mut self, c: char) -> Option<Result<Token, Error>>
    {
        match self.state
        {
            State::Initial =>
            {
                match c
                {
                    x if x.is_whitespace() => { self.source.next(); },
                    x if is_name_char(x) => self.state = State::Name,

                    ';' | '\n' => return self.single_char(TokenType::SemiColon, ";"),
                    '&' => { self.source.next(); self.state = State::And },
                    '|' => { self.source.next(); self.state = State::Pipe },
                    '$' => { self.source.next(); self.state = State::Variable },
                    '"' => { self.source.next(); self.state = State::String },
                    '\0' => return None,

                    _ => 
                    {
                        self.source.next();
                        return Some(Err(UnexpectedError::new_char(c)))
                    },
                }
            },

            State::Name =>
            {
                if !is_name_char(c)
                {
                    if c != '=' {
                        return Token::new(TokenType::Identifier, &self.buffer)
                    }
                    
                    self.source.next();
                    self.state = State::Assignement;
                }
                else
                {
                    self.buffer.push(c);
                    self.source.next();
                }
            },

            State::And => 
            {
                self.source.next();
                if c != '&' {
                    return Token::new(TokenType::And, "&")
                }
                self.source.next();
                return Token::new(TokenType::DoubleAnd, "&&")
            },

            State::Pipe =>
            {
                self.source.next();
                if c != '|' {
                    return Token::new(TokenType::Pipe, "|")
                }
                self.source.next();
                return Token::new(TokenType::DoublePipe, "||")
            }

            State::Variable =>
            {
                if !is_name_char(c) {
                    return Token::new(TokenType::Variable, &self.buffer)
                }
                self.buffer.push(c);
                self.source.next();
            },

            State::Assignement => 
            {
                if !is_name_char(c)
                {
                    return Some(Ok(Token
                    {
                        token_type: TokenType::Assignement,
                        data: self.buffer.clone(),
                        value: Some(self.value_buffer.clone()),
                    }));
                }
                self.value_buffer.push(c);
                self.source.next();
            },

            State::String => 
            {
                self.source.next();
                if c == '"' || c == '\0' {
                    return Token::new(TokenType::Identifier, &self.buffer);
                }
                self.buffer.push(c);
            },
        }

        None
    }

    fn get_next_char(&mut self) -> Result<Option<u8>, String>
    {
        let next_or_none = self.source.peek();
        if next_or_none.is_none() {
            return Ok(None);
        }

        let next_or_error = next_or_none.unwrap().as_ref();
        if next_or_error.is_err() {
            Err(next_or_error.unwrap_err().to_string())
        } else {
            Ok(Some(*next_or_error.ok().unwrap()))
        }
    }

}

impl<S: Read> Iterator for Lexer<S>
{
    type Item = Result<Token, Error>;

    fn next(&mut self) -> Option<Result<Token, Error>>
    {
        // Reset state
        self.state = State::Initial;
        self.buffer = String::new();

        loop
        {
            let next_or_error = self.get_next_char();
            if next_or_error.is_err() {
                return Some(Err(Error::IO(next_or_error.unwrap_err())));
            }

            let next_or_none = next_or_error.unwrap();
            let next = next_or_none.unwrap_or(0);
            let token = self.handle_char(next as char);
            if token.is_none() 
            {
                if next_or_none.is_none() {
                    return None;
                }
                continue;
            }

            return token;
        }
    }

}
