//
// Created by rafaelsamenezes on 21/09/2021.
//

#ifndef ESBMC_JIMPLE_DECLARATION_H
#define ESBMC_JIMPLE_DECLARATION_H

#include <jimple-frontend/AST/jimple_method_body.h>
#include <jimple-frontend/AST/jimple_type.h>

class jimple_declaration : public jimple_method_field {
  virtual void from_json(const json& j) override;
  virtual std::string to_string() override;

protected:
  std::shared_ptr<jimple_type> t;
  std::vector<std::string> names;
};

#endif //ESBMC_JIMPLE_DECLARATION_H
