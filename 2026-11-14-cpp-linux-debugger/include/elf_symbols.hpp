// elf_symbols.hpp - just enough ELF64 parsing to turn "main" into an address.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace minidbg {

enum class symbol_type { function, object };

struct symbol {
    std::string name;
    symbol_type type;
    std::uint64_t value;  // st_value: link-time address (an offset from the load base if PIE)
    std::uint64_t size;
};

class elf_symbols {
public:
    // Throws std::runtime_error if the file is missing or not an x86-64 ELF.
    explicit elf_symbols(const std::string& path);

    bool is_pie() const { return m_pie; }
    std::uint64_t entry() const { return m_entry; }
    const std::vector<symbol>& all() const { return m_symbols; }
    std::optional<symbol> find(const std::string& name) const;
    // The function containing a link-time address, if any (used for "where am I?").
    std::optional<symbol> function_at(std::uint64_t link_addr) const;

private:
    bool m_pie = false;
    std::uint64_t m_entry = 0;
    std::vector<symbol> m_symbols;
};

}  // namespace minidbg
