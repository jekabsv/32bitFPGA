#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <map>
#include <cctype>
#include <limits>
#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif


using namespace std;

static inline string trim(const string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static int regId(const string& r)
{
    if (r == "_" || r == "0") return 0;
    if (r.size() == 1 && r[0] >= 'A' && r[0] <= 'Z') return 1 + (r[0] - 'A');
    if (r.size() > 1 && r[0] == 'R') {
        int v = 0;
        for (size_t i = 1; i < r.size(); ++i)
            if (isdigit((unsigned char)r[i])) v = v * 10 + (r[i] - '0');
        return min(31, max(0, v));
    }
    return -1;
}
static bool parseInt(const string& s, long long& out) {
    string t = s; bool neg = false;
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) { neg = t[0] == '-'; t = t.substr(1); }
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        long long v = 0;
        for (size_t i = 2; i < t.size(); ++i) {
            char c = t[i]; int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            v = (v << 4) | d;
        }
        out = neg ? -v : v; return true;
    }
    if (t.empty()) return false;
    long long v = 0;
    for (char c : t) { if (!isdigit((unsigned char)c)) return false; v = v * 10 + (c - '0'); }
    out = neg ? -v : v; return true;
}
static uint32_t sign_pack16(long long v) { return (uint32_t)(v & 0xFFFF); }
static uint32_t sign_pack26(long long v) { return (uint32_t)(v & ((1LL << 26) - 1)); }

static int op6(const string& m) {
    string u = m; for (auto& c : u) c = toupper(c);
    if (u == "ADDI")return 0b000010;
    if (u == "SUBI")return 0b000100;
    if (u == "ANDI")return 0b000110;
    if (u == "ORI") return 0b000111;
    if (u == "MOV") return 0b001000;
    if (u == "XORI")return 0b001001;
    if (u == "CMPI")return 0b001010;
    if (u == "CMPIU")return 0b001011;
    if (u == "LDI") return 0b001100;
    if (u == "IN")  return 0b010001;
    if (u == "OUT") return 0b010010;
    if (u == "READ") return 0b010011;   // READ d t imm : d = MEM[t+imm]
    if (u == "WRITE")return 0b010100;   // WRITE d t imm : MEM[d+imm] = t
    if (u == "JMP") return 0b010101;
    if (u == "JMPN")return 0b010110;
    if (u == "JMPE")return 0b010111;
    if (u == "JMPG")return 0b011000;
    if (u == "BAL") return 0b011001;
    if (u == "BRE") return 0b011010;
    if (u == "BRN") return 0b011011;
    if (u == "BRG") return 0b011100;
    return -1;
}
static int func5(const string& m) {
    string u = m; for (auto& c : u) c = toupper(c);
    if (u == "ADD") return 0b00000;
    if (u == "SUB") return 0b00010;
    if (u == "DEC") return 0b00011;
    if (u == "AND") return 0b00100;
    if (u == "OR")  return 0b00101;
    if (u == "NOT") return 0b00110;
    if (u == "XOR") return 0b00111;
    if (u == "SLL") return 0b01000;
    if (u == "SRL") return 0b01001;
    if (u == "SRA") return 0b01010;
    if (u == "RCL") return 0b01011;
    if (u == "RCR") return 0b01100;
    if (u == "CMP") return 0b01110;
    if (u == "CMPU")return 0b01111;
    return -1;
}

static void wait_any_key() {
#ifdef _WIN32
    cout << "Press any key to exit..."; cout.flush();
    _getch();
#else
    cout << "Press any key to exit..."; cout.flush();
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    char ch;
    ::read(STDIN_FILENO, &ch, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    cout << "\n";
}


int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << "[FPGA 32-bit Assembler REPL] Type assembly, /done to compile.\n";
    vector<string> rawLines;
    string line;
    while (true) {
        cout << "> ";
        if (!getline(cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;
        if (line == "/done") break;
        rawLines.push_back(line);
    }

    // ===== First pass: collect labels and produce instruction-only lines =====
    map<string, int> label2addr;
    vector<string> lines; // pure instruction lines in order
    for (const auto& raw : rawLines) {
        string s = trim(raw);
        if (s.empty()) continue;

        // Simple comment kill: everything after ';'
        size_t sc = s.find(';');
        if (sc != string::npos) s = trim(s.substr(0, sc));
        if (s.empty()) continue;

        // Label detection: "label:" possibly followed by instruction
        size_t colon = s.find(':');
        if (colon != string::npos) {
            string left = trim(s.substr(0, colon));
            if (!left.empty() && left.find_first_of(" \t\r\n") == string::npos) {
                // record label at current instruction address
                if (label2addr.count(left)) {
                    cerr << "Warning: duplicate label '" << left << "', last one wins.\n";
                }
                label2addr[left] = (int)lines.size();
                s = trim(s.substr(colon + 1));
                if (s.empty()) continue; // label-only line
            }
        }
        if (!s.empty()) lines.push_back(s);
    }

    vector<uint32_t> words;
    words.reserve(lines.size());

    for (size_t i = 0; i < lines.size(); ++i) {
        string s = lines[i];
        stringstream ss(s);
        string op; ss >> op;
        if (op.empty()) continue;
        string U = op; for (auto& c : U) c = toupper(c);
        vector<string> toks; string tk;
        while (ss >> tk) toks.push_back(tk);

        uint32_t w = 0;

        auto assemble_branch_imm_or_label = [&](const string& tok, long long& outOff)->bool {
            // Accept immediate, else label
            if (parseInt(tok, outOff)) return true;
            auto it = label2addr.find(tok);
            if (it == label2addr.end()) return false;
            // PC is already incremented once before execution, so offset is:
            // target - (current + 1)
            outOff = (long long)it->second - (long long)(i + 1);
            return true;
            };

        // I-type
        if (U == "LDI") {
            int d = 0; long long imm = 0;
            if (toks.size() == 2) { d = regId(toks[0]); parseInt(toks[1], imm); }
            else if (toks.size() == 3) { d = regId(toks[0]); parseInt(toks[2], imm); }
            w |= (0b001100u << 26) | ((d & 0x1F) << 21) | (0u << 16) | sign_pack16(imm);
        }
        else if (U == "MOV") {
            int d = regId(toks[0]), t = regId(toks[1]);
            w |= (0b001000u << 26) | ((d & 0x1F) << 21) | ((t & 0x1F) << 16);
        }
        else if (U == "ADDI" || U == "SUBI" || U == "ANDI" || U == "ORI" || U == "XORI" || U == "CMPI" || U == "CMPIU") {
            int d = regId(toks[0]), t = regId(toks[1]); long long imm = 0; parseInt(toks[2], imm);
            w |= (op6(U) << 26) | ((d & 0x1F) << 21) | ((t & 0x1F) << 16) | sign_pack16(imm);
        }
        else if (U == "IN") {
            long long id = 0; parseInt(toks[0], id); int d = regId(toks[1]);
            w |= (0b010001u << 26) | ((d & 0x1F) << 21) | (0u << 16) | sign_pack16(id);
        }
        else if (U == "OUT") {
            long long id = 0; parseInt(toks[0], id); int t = regId(toks[1]);
            w |= (0b010010u << 26) | (0u << 21) | ((t & 0x1F) << 16) | sign_pack16(id);
        }
        else if (U == "READ") {
            // READ d t imm   -> d = MEM[t + imm]
            if (toks.size() < 3) { cout << "Unknown op: " << U << "\n"; continue; }
            int d = regId(toks[0]);
            int t = regId(toks[1]);
            long long imm = 0;
            if (!parseInt(toks[2], imm)) { cout << "Invalid immediate for " << U << "\n"; continue; }
            if (d == 0) {
                // writing into R0 (destination) will be ignored on the CPU; warn assembler user
                cerr << "Warning: READ into R0 (register 0) will be ignored by CPU.\n";
            }
            w |= (0b010011u << 26) | ((d & 0x1F) << 21) | ((t & 0x1F) << 16) | sign_pack16(imm);
        }
        else if (U == "WRITE") {
            // WRITE d t imm  -> MEM[d + imm] = t
            if (toks.size() < 3) { cout << "Unknown op: " << U << "\n"; continue; }
            int d = regId(toks[0]); // base address register (rd field)
            int t = regId(toks[1]); // source register to write from (rt field)
            long long imm = 0;
            if (!parseInt(toks[2], imm)) { cout << "Invalid immediate for " << U << "\n"; continue; }
            // NOTE: writes target memory, not registers. But if user passed R0 as 't' that's fine (will write 0).
            w |= (0b010100u << 26) | ((d & 0x1F) << 21) | ((t & 0x1F) << 16) | sign_pack16(imm);
        }
        else if (U == "BAL" || U == "BRE" || U == "BRN" || U == "BRG" || U == "JMP" || U == "JMPN" || U == "JMPE" || U == "JMPG") {
            if (toks.empty()) { cout << "Unknown op: " << U << "\n"; continue; }
            long long off = 0;
            if (!assemble_branch_imm_or_label(toks[0], off)) {
                cout << "Unknown label/immediate for " << U << ": " << toks[0] << "\n";
                continue;
            }
            w |= (op6(U) << 26) | sign_pack26(off);
        }
        else if (U == "NOP") {
            w = 0;
        }
        // R-type with optional trailing "1" to set flags
        else if (U == "ADD" || U == "SUB" || U == "DEC" || U == "AND" || U == "OR" || U == "NOT" ||
            U == "XOR" || U == "SLL" || U == "SRL" || U == "SRA" || U == "RCL" || U == "RCR" ||
            U == "CMP" || U == "CMPU")
        {
            int opcode = 0b000001;
            int d = 0, t = 0, sr = 0, sh = 0;
            bool setFlags = (U == "CMP" || U == "CMPU");

            if (U == "SLL" || U == "SRL" || U == "SRA" || U == "RCL" || U == "RCR") {
                // d t sh [1]
                if (toks.size() < 3) { cout << "Unknown op: " << U << "\n"; continue; }
                d = regId(toks[0]); t = regId(toks[1]);
                long long shv = 0; parseInt(toks[2], shv); sh = (int)shv & 31;
                if (toks.size() >= 4) { long long v; if (parseInt(toks[3], v) && v == 1) setFlags = true; }
                sr = 0;
            }
            else if (U == "NOT" || U == "DEC") {
                // d t [1]
                if (toks.size() < 2) { cout << "Unknown op: " << U << "\n"; continue; }
                d = regId(toks[0]); t = regId(toks[1]); sr = 0; sh = 0;
                if (toks.size() >= 3) { long long v; if (parseInt(toks[2], v) && v == 1) setFlags = true; }
            }
            else {
                // d t s [1]
                if (toks.size() < 3) { cout << "Unknown op: " << U << "\n"; continue; }
                d = regId(toks[0]); t = regId(toks[1]); sr = regId(toks[2]); sh = 0;
                if (toks.size() >= 4) { long long v; if (parseInt(toks[3], v) && v == 1) setFlags = true; }
            }
            int f = func5(U);
            if (f < 0) { cout << "Unknown op: " << U << "\n"; continue; }
            w |= (opcode << 26) | ((d & 0x1F) << 21) | ((t & 0x1F) << 16) | ((sr & 0x1F) << 11)
                | ((sh & 0x1F) << 6) | ((uint32_t)(f & 0x1F) << 1) | (setFlags ? 1u : 0u);
        }
        else {
            cout << "Unknown op: " << U << "\n";
            continue;
        }

        // Echo assembled word
        {
            string bin; bin.reserve(32);
            for (int b = 31; b >= 0; --b) bin.push_back((w >> b) & 1 ? '1' : '0');
            cout << setw(6) << left << U << " -> " << bin << "  " << hex << uppercase << setw(8) << setfill('0') << w << dec << setfill(' ');
        }
        cout << "\n";
        words.push_back(w);
    }

    // ===== Final dump in "v3.0 hex words addressed" format (lowercase), 8 words per line =====
    size_t pad = (8 - (words.size() % 8)) % 8;
    words.insert(words.end(), pad, 0u);

    cout << "\nv3.0 hex words addressed\n";
    for (size_t i = 0; i < words.size(); i += 8) {
        unsigned a = (unsigned)i;
        std::ostringstream lab;
        lab << std::hex << std::nouppercase << setw(2) << setfill('0') << (a & 0xFF);
        cout << lab.str() << ": ";
        for (size_t j = 0; j < 8; ++j) {
            std::ostringstream whex;
            whex << std::hex << std::nouppercase << setw(8) << setfill('0') << words[i + j];
            cout << whex.str();
            if (j < 7) cout << " ";
        }
        cout << "\n";
    }
    wait_any_key();
    return 0;

}
