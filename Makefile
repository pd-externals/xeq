lib.name = xeq

shared = \
shared/sq.c \
shared/dict.c \
shared/bifi.c \
shared/mifi.c \
shared/mfbb.c \
shared/hyphen.c \
shared/text.c \
shared/vefl.c
             
xeqsources = \
src/xeq.c \
src/xeq_data.c \
src/xeq_follow.c \
src/xeq_host.c \
src/xeq_parse.c \
src/xeq_polyparse.c \
src/xeq_polytempo.c \
src/xeq_query.c \
src/xeq_record.c \
src/xeq_time.c

xeq.class.sources = $(xeqsources) $(shared)

cflags = -I./shared

datafiles = \
LICENSE.txt \
README.md \
$(wildcard doc/*-help.pd) \
doc/output~.pd \
doc/xeq.pdf

datadirs = \
examples/ql \
examples/mf \
examples/a 

suppress-wunused = yes

include Makefile.pdlibbuilder

install: install-aliases

# on Linux, add symbolic links for lower case aliases
install-aliases: all
	cp -R examples/a $(installpath)
	cp -R examples/mf $(installpath)
	cp -R examples/ql $(installpath)
	cp examples/*.pd $(installpath)/examples