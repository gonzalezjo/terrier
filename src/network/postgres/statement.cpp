#include "network/postgres/statement.h"

namespace terrier::network {

Statement::Statement(std::string &&query_text, std::unique_ptr<parser::ParseResult> &&parse_result,
                     std::vector<type::TypeId> &&param_types)
    : query_text_(std::move(query_text)), parse_result_(std::move(parse_result)), param_types_(std::move(param_types)) {
  TERRIER_ASSERT(parse_result_ != nullptr, "It didn't parse. Why are we making a Statement object?");
  TERRIER_ASSERT(parse_result_->GetStatements().size() <= 1, "We currently expect one statement per string.");
  if (!Empty()) {
    root_statement_ = parse_result_->GetStatement(0);
    type_ = trafficcop::TrafficCopUtil::QueryTypeForStatement(root_statement_);
  }
}
}  // namespace terrier::network
