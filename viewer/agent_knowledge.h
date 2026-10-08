#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace synthcad {

class KnowledgeError : public std::runtime_error {
 public:
  KnowledgeError(std::string category, const std::string& message)
      : std::runtime_error(message), code(std::move(category)) {}
  std::string code;
};

// Data objects for the standard CLI response envelope. No checkout or session
// is needed: the source documents are compiled into the executable.
nlohmann::json ListDocs();
nlohmann::json ReadDoc(const std::string& topic);

}  // namespace synthcad
