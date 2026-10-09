// Compare.h - comparing the text of two documents.
//
// Both documents are read as sequences of words and compared with the
// Myers difference algorithm (as used by "diff" and git). The result is a
// copy of the *other* document with every difference marked as a standard
// PDF annotation, so it can be read in any viewer and listed with
// Edit PDF > All comments:
//   * added text: green highlight;
//   * changed text: orange highlight, the old text in its comment;
//   * removed text: a note where it was, holding the removed words.
// Runs on the render worker thread.
#pragma once
#include "PdfEngine.h"

void CompareDocuments(PdfEngine& current, const std::wstring& otherPath, const std::string& otherPassword,
                      const std::wstring& outPath, CompareResult& result);
