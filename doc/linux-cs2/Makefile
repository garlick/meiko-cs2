DEP =	report.tex project.bib timer.eps 256way.eps \
	iomem.eps busarch.eps bargraph.eps \
	canpkt.eps canaddr.eps canarch.eps

all : report.dvi report.ps

report.dvi : $(DEP)
	latex report
	bibtex report
	latex report
	latex report

%.eps: %.obj
	tgif -print -eps $<

%.ps : %.dvi
	dvips -o $(@F) $(<F)

view: report.ps
	ghostview report &

print: report.ps
	lpr $<

clean: 
	rm -f *~ *.dvi *.log *.aux *.ps *.bbl *.blg *.eps
