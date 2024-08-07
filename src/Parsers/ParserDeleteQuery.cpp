#include <Parsers/ParserDeleteQuery.h>
#include <Parsers/ASTDeleteQuery.h>
#include <Parsers/parseDatabaseAndTableName.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserSetQuery.h>


namespace DB
{

bool ParserDeleteQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    auto query = std::make_shared<ASTDeleteQuery>();
    node = query;

    ParserKeyword s_delete("DELETE");
    ParserKeyword s_from("FROM");
    ParserKeyword s_where("WHERE");
    ParserKeyword s_limit("LIMIT");
    ParserExpression parser_exp_elem;
    ParserKeyword s_settings("SETTINGS");

    ParserNumber number_parser;

    if (s_delete.ignore(pos, expected))
    {
        if (!s_from.ignore(pos, expected))
            return false;

        if (!parseDatabaseAndTableName(pos, expected, query->database, query->table))
            return false;

        if (!s_where.ignore(pos, expected))
            return false;

        if (!parser_exp_elem.parse(pos, query->predicate, expected))
            return false;

        /// ignore the limit clause from MYSQL delete SQL
        if (s_limit.ignore(pos, expected))
        {
            if (!number_parser.ignore(pos, expected))
                return false;
        }

        if (s_settings.ignore(pos, expected))
        {
            ParserSetQuery parser_settings(true);

            if (!parser_settings.parse(pos, query->settings_ast, expected))
                return false;
        }
    }
    else
        return false;

    if (query->predicate)
        query->children.push_back(query->predicate);

    // if (!query->database.empty())
    //     query->children.push_back(query->database);

    // if (query->table)
    //     query->children.push_back(query->table);

    if (query->settings_ast)
        query->children.push_back(query->settings_ast);

    return true;
}

}
