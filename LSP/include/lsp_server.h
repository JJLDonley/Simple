#ifndef SIMPLE_LSP_SERVER_H
#define SIMPLE_LSP_SERVER_H

#include <istream>
#include <ostream>

namespace Simple::LSP {

// Runs the stdio JSON-RPC language server loop.
// Returns 0 on normal shutdown, non-zero on protocol/IO errors.
int RunServer(std::istream& in, std::ostream& out);

} // namespace Simple::LSP

#endif // SIMPLE_LSP_SERVER_H

