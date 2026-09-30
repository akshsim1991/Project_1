#!/usr/bin/env python3
"""Generates the PDFs used by the instrumentation tests (needs reportlab).

  sample.pdf  3 pages of text, an outline, an internal link and a web link
  locked.pdf  1 page, user password "secret"

Run from FeatherPDF-Android:  python tools/make_test_pdfs.py
"""
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfgen import canvas

OUT = "app/src/androidTest/assets/"


def sample():
    c = canvas.Canvas(OUT + "sample.pdf", pagesize=A4)
    c.setTitle("Feather sample")
    c.setAuthor("Feather PDF tests")
    w, h = A4
    for n in range(1, 4):
        if n == 3:
            c.setPageSize(landscape(A4))
            w, h = landscape(A4)
        c.bookmarkPage("p%d" % n)
        c.addOutlineEntry("Chapter %d" % n, "p%d" % n, level=0)
        c.setFont("Helvetica-Bold", 28)
        c.drawString(72, h - 100, "Chapter %d" % n)
        c.setFont("Helvetica", 14)
        for i in range(20):
            c.drawString(72, h - 150 - i * 22, "Line %d of page %d: the quick brown fox jumps over the lazy dog." % (i + 1, n))
        if n == 1:
            c.setFillColorRGB(0, 0.3, 0.8)
            c.drawString(72, 120, "Go to chapter 3")
            c.linkRect("", "p3", (70, 115, 180, 135), relative=0)
            c.drawString(72, 90, "Visit example.com")
            c.linkURL("https://example.com/", (70, 85, 200, 105), relative=0)
            c.setFillColorRGB(0, 0, 0)
        c.drawString(72, 50, "Needle" if n == 2 else "Haystack")
        c.showPage()
    c.save()


def locked():
    c = canvas.Canvas(OUT + "locked.pdf", pagesize=A4, encrypt="secret")
    c.setFont("Helvetica", 20)
    c.drawString(72, 700, "Unlocked with the password.")
    c.showPage()
    c.save()


if __name__ == "__main__":
    sample()
    locked()
