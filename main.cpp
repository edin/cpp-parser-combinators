#include <string>
#include <sstream>
#include <iostream>
#include <functional>
#include <vector>
#include <map>
#include <variant>

using namespace std;

struct ResultItem {
    string name = "";
    variant<string, vector<ResultItem>> value;
};

using ResultMap = vector<ResultItem>;

enum class ResultType {
    Success = 1,
    Failure = 2
};

struct Result {
    ResultType status;
    string matched;
    string rest;
    string error;
    ResultMap results;

    Result() {}
    Result(ResultType status, string matched, string rest, string error):
        status(status),
        matched(matched),
        rest(rest),
        error(error) {
    }

    static Result failure(string error) {
        return Result(ResultType::Failure, "", "", error);
    }

    static Result success(string matched, string rest) {
        return Result(ResultType::Success, matched, rest, "");
    }

    bool isFailure() const { return status == ResultType::Failure; }
    bool isSuccess() const { return status == ResultType::Success; }

    void add(string name, string value) {
        if (value.size() > 0) {
            results.push_back(ResultItem {name, value} );
        }
    }
    void add(string name, ResultMap value) {
        results.push_back(ResultItem {name, value} );
    }

    void combine(const Result& result) {
        for(auto &item : result.results) {
            this->results.push_back(item);
        }
    }
};

std::ostream &operator <<(std::ostream &o, const Result &result)
{
    o << "\n";
    o << "[  Result  ]\n";
    o << "===========================================\n";
    if (result.isFailure()) {
        o << "{\n Result: Failure,\n Error: " << result.error << "\n}\n";
    } else {
        o << "{\n Result: Success,\n Matched: " << result.matched <<  ",\n Rest: " << result.rest << "\n}\n";
        o << "\n";

        o << "[  AST  ]\n";
        o << "===========================================\n";

        std::function<void(const ResultMap&, int)> printVector;
        printVector = [&o, &printVector](const ResultMap& items, int level) -> void {
            for (auto &item : items) {
                if (item.value.index() == 0) {
                    o << std::string(level * 4, ' ') << item.name << ": \"" << std::get<0>(item.value) << "\" \n";
                } else {
                    o << std::string(level * 4, ' ') << item.name << ": {" << "\n";
                    printVector(std::get<1>(item.value), level + 1);
                    o << std::string(level * 4, ' ') << "}" << "\n";
                }
            }
        };

        printVector(result.results, 0);
        o << "===========================================\n";
    }
    return o;
}

using Parser = std::function<Result(string)>;

Parser parseChar(char ch) {
    return [ch](string source) -> Result {
        if (source.length() == 0) {
            return Result::failure("End of imput stream.");
        }

        auto firstChar = source[0];

        if (firstChar == ch) {
            return Result::success(string{firstChar}, source.substr(1));
        } else {
            stringstream error;
            error << "Expected '" << ch << "' but got '" << firstChar << "'";
            return Result::failure(error.str());
        }
    };
}


Parser andThen (Parser parser1, Parser parser2) {
    return [parser1, parser2](string source) -> Result {

        auto result1 = parser1(source);

        if (result1.isFailure()) {
            return result1;
        }

        auto result2 = parser2(result1.rest);

        if (result2.isFailure()) {
            return result2;
        } else {
            auto result = Result::success(result1.matched + result2.matched, result2.rest);

            result.combine(result1);
            result.combine(result2);
            return result;
        }
    };
}

Parser orElse(Parser parser1, Parser parser2) {
    return [parser1, parser2](string source) -> Result {

        auto result1 = parser1(source);

        if (result1.isSuccess()) {
            return result1;
        }

        auto result2 = parser2(source);
        return result2;
    };
}

Parser reduce(vector<Parser> parsers, std::function<Parser(Parser, Parser)> reducer) {
    auto result = parsers[0];
    for (int i = 1; i < parsers.size(); i++) {
        result = reducer(result, parsers[i]);
    }
    return result;
};

template <typename TInput, typename Mapper>
vector<Parser> mapWith(TInput source, Mapper mapper) {
    vector<Parser> result;
    for(auto e: source) {
        result.push_back(mapper(e));
    }
    return result;
}

Parser choice(vector<Parser> parsers) {
    return reduce(parsers, orElse);
}

Parser anyOf(string value) {
    return choice(mapWith(value, parseChar));
}

Parser anyOf(char start, char end) {
    vector<Parser> parsers;
    for (char ch = start; ch <= end; ch++) {
        parsers.push_back(parseChar(ch));
    }
    return choice(parsers);
}

Parser parseString(string value) {
    return reduce(mapWith(value, parseChar), andThen);
}

Parser sequence(vector<Parser> parsers) {
    return reduce(parsers, andThen);
}

Parser nullParser() {
    return [](string source) -> Result {
        return Result::success("", source);
    };
}

Parser opt(Parser parser) {
    return choice({parser, nullParser()});
}

Parser many(Parser parser) {
    return [parser](string source) -> Result {
        string matched = "";
        string input = source;
        ResultMap items;

        while (true) {
            auto result = parser(input);
            if (result.isFailure()) {
                auto result = Result::success(matched, input);
                result.results = items;
                return result;
            } else {
                matched = matched + result.matched;
                input = result.rest;

                if (result.results.size() > 0) {
                    items.push_back(ResultItem{"item", result.results});
                }
            }
        }
    };
}


Parser many1(Parser parser) {
    return [parser](string source) -> Result {
        string matched = "";
        string input = source;

        while (true) {
            auto result = parser(input);
            if (result.isFailure()) {
                if (matched.length() == 0) {
                    return result;
                }
                auto result = Result::success(matched, input);
                return result;
            } else {
                matched = matched + result.matched;
                input = result.rest;
            }
        }
    };
}

Parser takeLeft (Parser parser1, Parser parser2) {
    return [parser1, parser2](string source) -> Result {
        auto result1 = parser1(source);
        auto result2 = parser1(result1.rest);
        return result2;
    };
}

Parser mapTo(Parser parser, string name) {
    return [parser, name](string source) -> Result {
        auto result = parser(source);
        if (result.isSuccess()) {
            if (result.results.size() == 0) {
                result.add(name, result.matched);
            } else {
                auto newResults = Result::success(result.matched, result.rest);
                newResults.add(name, result.results);
                return newResults;
            }
        }
        return result;
    };
}

Parser listOf(Parser whiteSpace, Parser parser, char separator) {

    auto separatorParser = parseChar(separator);

    return sequence({
        mapTo(
            opt(
                sequence({whiteSpace, parser})
            ),
            "item"
        ),
        many(
            sequence({
                whiteSpace, separatorParser,
                whiteSpace, parser
            })
        )
    });
}

Parser refParser(Parser &reference) {
    return [&reference](string source) -> Result {
        auto result = reference(source);
        return result;
    };
};

auto whiteSpace = opt(many(anyOf(" \t\r\n")));
auto digit  = anyOf('0', '9');
auto lower  = anyOf('a', 'z');
auto upper  = anyOf('A', 'Z');
auto letter = choice({lower, upper});

auto identifier    = sequence({
    letter,
    many(choice({letter, digit}))
});

auto integer = many1(digit);

auto structKeyword = parseString("struct");
auto constKeyword = parseString("const");
auto functionKeyword = parseString("function");


Parser parseBlock (Parser parser) {
    return sequence({
        whiteSpace, parseChar('{'),
        parser,
        whiteSpace, parseChar('}')
    });
}

Parser parseBinary(Parser parser, string op1, string op2, string type) {
    return  mapTo(
        sequence({
            mapTo(parser, "left"),
            many(
                sequence({
                    whiteSpace,
                    mapTo(choice({
                        parseString(op1),
                        parseString(op2)
                    }), "operator"),
                    whiteSpace,
                    mapTo(parser, "right")
                })
            )
        }),
        type
    );
}

extern Parser blockParser;
extern Parser expression;

auto parenExp = sequence({
    whiteSpace, parseChar('('),
    whiteSpace, refParser(expression),
    whiteSpace, parseChar(')')
});

auto value = choice({
    integer,
    identifier
});

auto mulExp = parseBinary(value,  "*",  "/",  "MulExpression");
auto addExp = parseBinary(mulExp, "+",  "-",  "AddExpression");
auto eqExp  = parseBinary(addExp, "==", "!=", "EqualityExpression");

Parser expression = eqExp;

auto parseIf = mapTo(
    sequence({
        whiteSpace, mapTo(parseString("if"), "type"),
        whiteSpace, mapTo(expression, "condition"),
        parseBlock(refParser(blockParser))
    }),
    "if"
);

auto parseFor = mapTo(
    sequence({
        whiteSpace, mapTo(parseString("for"), "type"),
        whiteSpace, mapTo(identifier, "variable"),
        whiteSpace, parseString("in"),
        whiteSpace, mapTo(value, "iterable"),
        parseBlock(refParser(blockParser))
    }),
    "for"
);


Parser blockParser = many(
    choice({
        parseIf,
        parseFor,
    })
);

auto parseParameter = mapTo(
    sequence({
        whiteSpace, mapTo(identifier, "type"),
        whiteSpace, mapTo(identifier, "name"),
    }),
    "parameter"
);

auto parseConst = mapTo(
    sequence({
        whiteSpace, mapTo(constKeyword, "type"),
        whiteSpace, mapTo(identifier, "name"),
        whiteSpace, parseChar('='),
        whiteSpace, mapTo(integer, "value")
    }),
    "const"
);

auto parseField = sequence({
    whiteSpace, mapTo(identifier, "name"),
    whiteSpace, mapTo(identifier, "field"),
    whiteSpace, parseChar(';')
});

auto parseFunction = mapTo(
    sequence({
        whiteSpace, mapTo(functionKeyword, "type"),
        whiteSpace, mapTo(identifier, "name"),
        whiteSpace, parseChar('('),
        mapTo(listOf(whiteSpace, parseParameter, ','), "parameters"),
        whiteSpace, parseChar(')'),
        parseBlock(
            refParser(blockParser)
        )
    }),
    "function"
);

auto parseStruct = mapTo(
    sequence({
        whiteSpace, mapTo(structKeyword, "type"),
        whiteSpace, mapTo(identifier, "name"),
        parseBlock(
            many(
                choice({
                    parseField,
                    parseFunction
                })
            )
        )
    }),
    "struct"
);

auto parse = mapTo(
    many(
        choice({
            parseStruct,
            parseConst,
            parseFunction
        })
    ),
    "ast"
);

int main()
{


    string source = R""(

        const x = 100
        const y = 200

        struct Point {
            int x;
            int y;
        }

        struct Line {
            Point a;
            Point b;

            function toString() { }
            function interesect(Line other) { }
        }

        struct Triangle {
            Point a;
            Point b;
            Point c;
        }

        function main (int a, int b, int c) {
            if a * b * c * d + 5*5 == 1000 * 20 {

            }
        }
    )"";

    auto result = parse(source);

    cout << result;
}