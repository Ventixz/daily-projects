// elf_symbols.cpp - read .symtab (falling back to .dynsym) from an ELF64 file.
#include "elf_symbols.hpp"

#include <elf.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace minidbg {

namespace {

template <typename T>
T read_at(const std::vector<char>& file, std::uint64_t off) {
    if (off + sizeof(T) > file.size()) throw std::runtime_error("truncated ELF file");
    T out;
    std::memcpy(&out, file.data() + off, sizeof(T));
    return out;
}

}  // namespace

elf_symbols::elf_symbols(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto ehdr = read_at<Elf64_Ehdr>(file, 0);
    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) throw std::runtime_error(path + " is not an ELF file");
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64 || ehdr.e_machine != EM_X86_64)
        throw std::runtime_error(path + " is not an x86-64 ELF64 file");

    m_pie = ehdr.e_type == ET_DYN;  // PIE executables are "shared objects" that get a random base
    m_entry = ehdr.e_entry;

    std::vector<Elf64_Shdr> sections;
    for (unsigned i = 0; i < ehdr.e_shnum; ++i)
        sections.push_back(read_at<Elf64_Shdr>(file, ehdr.e_shoff + std::uint64_t(i) * ehdr.e_shentsize));

    auto load = [&](std::uint32_t wanted) {
        bool found = false;
        for (const auto& sh : sections) {
            if (sh.sh_type != wanted || sh.sh_entsize == 0) continue;
            found = true;
            const auto& strtab = sections.at(sh.sh_link);  // sh_link names the matching string table
            for (std::uint64_t off = 0; off + sizeof(Elf64_Sym) <= sh.sh_size; off += sh.sh_entsize) {
                auto sym = read_at<Elf64_Sym>(file, sh.sh_offset + off);
                int type = ELF64_ST_TYPE(sym.st_info);
                if ((type != STT_FUNC && type != STT_OBJECT) || sym.st_value == 0 || sym.st_name == 0) continue;
                std::uint64_t name_off = strtab.sh_offset + sym.st_name;
                if (name_off >= file.size()) continue;
                std::string name(file.data() + name_off, strnlen(file.data() + name_off, file.size() - name_off));
                m_symbols.push_back({name, type == STT_FUNC ? symbol_type::function : symbol_type::object,
                                     sym.st_value, sym.st_size});
            }
        }
        return found;
    };
    if (!load(SHT_SYMTAB)) load(SHT_DYNSYM);  // stripped binaries only keep .dynsym
}

std::optional<symbol> elf_symbols::find(const std::string& name) const {
    for (const auto& s : m_symbols)
        if (s.name == name) return s;
    return std::nullopt;
}

std::optional<symbol> elf_symbols::function_at(std::uint64_t link_addr) const {
    for (const auto& s : m_symbols)
        if (s.type == symbol_type::function && link_addr >= s.value && link_addr < s.value + s.size) return s;
    return std::nullopt;
}

}  // namespace minidbg
