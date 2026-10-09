// PdfCrypt.h - password protection: encrypts a PDF with the standard
// security handler, AES-256 (revision 6, as in PDF 2.0 and Acrobat X and
// later), which every current PDF reader opens.
//
// PDFium can read encrypted files but not write them, so the document is
// first saved unencrypted into memory by PDFium; this module then rewrites
// that file object by object, encrypting every string and stream, and adds
// the /Encrypt dictionary. It relies on the shape of PDFium's output (a
// classic cross-reference table, one object after the other) and refuses
// anything else rather than guess.
#pragma once
#include <cstdint>
#include <string>

// PDF permission bits (/P), 1-based bit numbers from the PDF specification.
enum : uint32_t {
    kPermPrint = 1u << 2,         // bit 3
    kPermModify = 1u << 3,        // bit 4
    kPermCopy = 1u << 4,          // bit 5
    kPermAnnotate = 1u << 5,      // bit 6
    kPermFillForms = 1u << 8,     // bit 9
    kPermAccessibility = 1u << 9, // bit 10
    kPermAssemble = 1u << 10,     // bit 11
    kPermPrintHigh = 1u << 11,    // bit 12
    kPermAll = 0xFFFFFFFFu,
};

// Encrypts `in` (a complete PDF file as written by PDFium) into `out`.
// `userPassword` opens the document (may be empty: anyone can open it, but
// the permissions still apply); `ownerPassword` lifts the restrictions (a
// random one is used when empty). Passwords are UTF-8.
bool EncryptPdf(const std::string& in, const std::string& userPassword, const std::string& ownerPassword,
                uint32_t permissions, std::string& out);
