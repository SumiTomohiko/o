
TC_DIR = $(top_srcdir)/tokyocabinet
DEFAULT_INCLUDES = -I$(top_srcdir)/include -I$(TC_DIR)

bin_PROGRAMS = o
o_SOURCES = o.c
o_CFLAGS = -Wall -Werror -g
o_LDFLAGS = -lo
lib_LTLIBRARIES = libo.la
libo_la_SOURCES = core.c
libo_la_CFLAGS = -Wall -Werror -g
libo_la_LIBADD = $(TC_DIR)/libtokyocabinet.a -lz -lbz2 -lrt -lpthread -lm -lc

# vim: tabstop=8 shiftwidth=8 noexpandtab
