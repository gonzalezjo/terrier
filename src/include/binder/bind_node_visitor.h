#pragma once

#include <memory>
#include <string>
#include <utility>
#include "binder/binder_context.h"
#include "catalog/catalog_defs.h"
#include "common/sql_node_visitor.h"
#include "parser/postgresparser.h"
#include "parser/statements.h"

namespace terrier {

namespace parser {
class SQLStatement;
class CaseExpression;
class ConstantValueExpression;
class ColumnValueExpression;
class SubqueryExpression;
class StarExpression;
class OperatorExpression;
class AggregateExpression;
}  // namespace parser

namespace catalog {
class CatalogAccessor;
}  // namespace catalog

namespace binder {

/**
 * Interface to be notified of the composition of a bind node.
 */
class BindNodeVisitor : public SqlNodeVisitor {
 public:
  /**
   * Initialize the bind node visitor object with a pointer to a catalog accessor, and a default database name
   * @param catalog_accessor Pointer to a catalog accessor
   * @param default_database_name Default database name
   */
  BindNodeVisitor(std::unique_ptr<catalog::CatalogAccessor> catalog_accessor, std::string default_database_name);
  ~BindNodeVisitor() override { delete context_; }

  /**
   * Perform binding on the passed in tree. Bind the ids according to the names in the tree.
   * For example, bind the corresponding database oid to an expression, which has a database name
   * @param tree Parsed in AST tree of the SQL statement
   */
  void BindNameToNode(common::ManagedPointer<parser::SQLStatement> tree, parser::ParseResult *parse_result);

  /**
   * This method is used by the QueryToOperatorTransformer to take ownership of the catalog accessor.
   * @return catalog accessor
   */
  std::unique_ptr<catalog::CatalogAccessor> GetCatalogAccessor() { return std::move(catalog_accessor_); }

  void Visit(parser::SelectStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::JoinDefinition *node, parser::ParseResult *parse_result) override;
  void Visit(parser::TableRef *node, parser::ParseResult *parse_result) override;
  void Visit(parser::GroupByDescription *node, parser::ParseResult *parse_result) override;
  void Visit(parser::OrderByDescription *node, parser::ParseResult *parse_result) override;
  void Visit(parser::LimitDescription *node, parser::ParseResult *parse_result) override;
  void Visit(parser::CreateStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::CreateFunctionStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::InsertStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::DeleteStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::DropStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::PrepareStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::ExecuteStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::TransactionStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::UpdateStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::CopyStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::AnalyzeStatement *node, parser::ParseResult *parse_result) override;
  void Visit(parser::CaseExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::SubqueryExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::ConstantValueExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::ColumnValueExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::StarExpression *expr, parser::ParseResult *parse_result) override;
  // TODO(Ling): implement this after we add support for function expression
  // void Visit(parser::FunctionExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::OperatorExpression *expr, parser::ParseResult *parse_result) override;
  void Visit(parser::AggregateExpression *expr, parser::ParseResult *parse_result) override;

 private:
  /** Current context of the query or subquery */
  BinderContext *context_ = nullptr;
  /** Catalog accessor */
  std::unique_ptr<catalog::CatalogAccessor> catalog_accessor_;
  /** Default database name of the query. Default to current database reside in */
  std::string default_database_name_;
};

}  // namespace binder
}  // namespace terrier
