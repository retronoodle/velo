CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=velo
SRC=src
TESTDIR=tests
SOURCES=$(SRC)/clipcodec.c $(SRC)/velo.c $(SRC)/term.c $(SRC)/buffer.c $(SRC)/ui.c $(SRC)/files.c $(SRC)/undo.c $(SRC)/clipboard.c $(SRC)/search.c $(SRC)/utf8.c $(SRC)/menu.c $(SRC)/options.c $(SRC)/recovery.c $(SRC)/mouse.c $(SRC)/tabs.c $(SRC)/recent.c $(SRC)/syntax.c $(SRC)/gitinfo.c $(SRC)/gitmodal.c $(SRC)/watch.c $(SRC)/aisetup.c $(SRC)/ai.c $(SRC)/aiask.c $(SRC)/aiwait.c $(SRC)/aiedit.c $(SRC)/aiexplain.c $(SRC)/viewer.c $(SRC)/fuzzy.c $(SRC)/quickopen.c $(SRC)/diffcap.c $(SRC)/gitpush.c $(SRC)/msgedit.c $(SRC)/brace.c $(SRC)/aiasync.c $(SRC)/aicomplete.c $(SRC)/aitools.c $(SRC)/aichat.c $(SRC)/chatmd.c $(SRC)/aiinstr.c $(SRC)/linediff.c $(SRC)/filetree.c $(SRC)/fileops.c $(SRC)/sidebar.c $(SRC)/plans.c $(SRC)/aiweb.c $(SRC)/aifetch.c $(SRC)/gitstat.c
HEADERS=$(SRC)/clipcodec.h $(SRC)/editor.h $(SRC)/term.h $(SRC)/buffer.h $(SRC)/ui.h $(SRC)/files.h $(SRC)/undo.h $(SRC)/clipboard.h $(SRC)/search.h $(SRC)/utf8.h $(SRC)/menu.h $(SRC)/options.h $(SRC)/recovery.h $(SRC)/mouse.h $(SRC)/tabs.h $(SRC)/recent.h $(SRC)/syntax.h $(SRC)/gitinfo.h $(SRC)/gitmodal.h $(SRC)/watch.h $(SRC)/aisetup.h $(SRC)/ai.h $(SRC)/aiask.h $(SRC)/aiwait.h $(SRC)/aiedit.h $(SRC)/aiexplain.h $(SRC)/viewer.h $(SRC)/fuzzy.h $(SRC)/quickopen.h $(SRC)/diffcap.h $(SRC)/gitpush.h $(SRC)/msgedit.h $(SRC)/brace.h $(SRC)/aiasync.h $(SRC)/aicomplete.h $(SRC)/aitools.h $(SRC)/aichat.h $(SRC)/chatmd.h $(SRC)/aiinstr.h $(SRC)/linediff.h $(SRC)/filetree.h $(SRC)/fileops.h $(SRC)/sidebar.h $(SRC)/plans.h $(SRC)/aiweb.h $(SRC)/aifetch.h $(SRC)/gitstat.h
TEST=test_utf8
TEST_SYNTAX=test_syntax
TEST_OPTIONS=test_options
TEST_CLIPCODEC=test_clipcodec
TEST_AI=test_ai
TEST_FUZZY=test_fuzzy
TEST_DIFFCAP=test_diffcap
TEST_GITPUSH=test_gitpush
TEST_MSGEDIT=test_msgedit
TEST_BRACE=test_brace
TEST_AITOOLS=test_aitools
TEST_CHATMD=test_chatmd
TEST_AIINSTR=test_aiinstr
TEST_LINEDIFF=test_linediff
TEST_FILETREE=test_filetree
TEST_FILEOPS=test_fileops
TEST_PLANS=test_plans
TEST_AIWEB=test_aiweb
TEST_GITSTAT=test_gitstat

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(SRC) -o $(TARGET) $(SOURCES)

$(TEST): $(TESTDIR)/test_utf8.c $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST) $(TESTDIR)/test_utf8.c $(SRC)/utf8.c

$(TEST_SYNTAX): $(TESTDIR)/test_syntax.c $(SRC)/syntax.c $(SRC)/syntax.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_SYNTAX) $(TESTDIR)/test_syntax.c $(SRC)/syntax.c

$(TEST_OPTIONS): $(TESTDIR)/test_options.c $(SRC)/options.c $(SRC)/options.h $(SRC)/recent.c $(SRC)/recent.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_OPTIONS) $(TESTDIR)/test_options.c $(SRC)/options.c $(SRC)/recent.c $(SRC)/utf8.c

$(TEST_CLIPCODEC): $(TESTDIR)/test_clipcodec.c $(SRC)/clipcodec.c $(SRC)/clipcodec.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_CLIPCODEC) $(TESTDIR)/test_clipcodec.c $(SRC)/clipcodec.c

$(TEST_AI): $(TESTDIR)/test_ai.c $(SRC)/ai.c $(SRC)/ai.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_AI) $(TESTDIR)/test_ai.c $(SRC)/ai.c $(SRC)/utf8.c

$(TEST_FUZZY): $(TESTDIR)/test_fuzzy.c $(SRC)/fuzzy.c $(SRC)/fuzzy.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_FUZZY) $(TESTDIR)/test_fuzzy.c $(SRC)/fuzzy.c

$(TEST_DIFFCAP): $(TESTDIR)/test_diffcap.c $(SRC)/diffcap.c $(SRC)/diffcap.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_DIFFCAP) $(TESTDIR)/test_diffcap.c $(SRC)/diffcap.c

$(TEST_GITPUSH): $(TESTDIR)/test_gitpush.c $(SRC)/gitpush.c $(SRC)/gitpush.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_GITPUSH) $(TESTDIR)/test_gitpush.c $(SRC)/gitpush.c

$(TEST_BRACE): $(TESTDIR)/test_brace.c $(SRC)/brace.c $(SRC)/brace.h $(SRC)/editor.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_BRACE) $(TESTDIR)/test_brace.c $(SRC)/brace.c

$(TEST_MSGEDIT): $(TESTDIR)/test_msgedit.c $(SRC)/msgedit.c $(SRC)/msgedit.h $(SRC)/utf8.c $(SRC)/utf8.h $(SRC)/clipcodec.c $(SRC)/clipcodec.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_MSGEDIT) $(TESTDIR)/test_msgedit.c $(SRC)/msgedit.c $(SRC)/utf8.c $(SRC)/clipcodec.c

$(TEST_AITOOLS): $(TESTDIR)/test_aitools.c $(SRC)/aitools.c $(SRC)/aitools.h $(SRC)/ai.c $(SRC)/ai.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_AITOOLS) $(TESTDIR)/test_aitools.c $(SRC)/aitools.c $(SRC)/ai.c $(SRC)/utf8.c

$(TEST_CHATMD): $(TESTDIR)/test_chatmd.c $(SRC)/chatmd.c $(SRC)/chatmd.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_CHATMD) $(TESTDIR)/test_chatmd.c $(SRC)/chatmd.c $(SRC)/utf8.c

$(TEST_AIINSTR): $(TESTDIR)/test_aiinstr.c $(SRC)/aiinstr.c $(SRC)/aiinstr.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_AIINSTR) $(TESTDIR)/test_aiinstr.c $(SRC)/aiinstr.c

$(TEST_LINEDIFF): $(TESTDIR)/test_linediff.c $(SRC)/linediff.c $(SRC)/linediff.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_LINEDIFF) $(TESTDIR)/test_linediff.c $(SRC)/linediff.c

$(TEST_FILETREE): $(TESTDIR)/test_filetree.c $(SRC)/filetree.c $(SRC)/filetree.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_FILETREE) $(TESTDIR)/test_filetree.c $(SRC)/filetree.c

$(TEST_FILEOPS): $(TESTDIR)/test_fileops.c $(SRC)/fileops.c $(SRC)/fileops.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_FILEOPS) $(TESTDIR)/test_fileops.c $(SRC)/fileops.c

$(TEST_PLANS): $(TESTDIR)/test_plans.c $(SRC)/plans.c $(SRC)/plans.h $(SRC)/recent.c $(SRC)/recent.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_PLANS) $(TESTDIR)/test_plans.c $(SRC)/plans.c $(SRC)/recent.c

$(TEST_AIWEB): $(TESTDIR)/test_aiweb.c $(SRC)/aiweb.c $(SRC)/aiweb.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_AIWEB) $(TESTDIR)/test_aiweb.c $(SRC)/aiweb.c

$(TEST_GITSTAT): $(TESTDIR)/test_gitstat.c $(SRC)/gitstat.c $(SRC)/gitstat.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_GITSTAT) $(TESTDIR)/test_gitstat.c $(SRC)/gitstat.c

test: $(TEST_GITSTAT) $(TEST_AIWEB) $(TEST_PLANS) $(TEST_FILETREE) $(TEST_FILEOPS) $(TEST_LINEDIFF) $(TEST_AIINSTR) $(TEST_CHATMD) $(TEST_AITOOLS) $(TEST_BRACE) $(TEST) $(TEST_SYNTAX) $(TEST_OPTIONS) $(TEST_CLIPCODEC) $(TEST_AI) $(TEST_FUZZY) $(TEST_DIFFCAP) $(TEST_GITPUSH) $(TEST_MSGEDIT)
	./$(TEST)
	./$(TEST_SYNTAX)
	./$(TEST_OPTIONS)
	./$(TEST_CLIPCODEC)
	./$(TEST_AI)
	./$(TEST_FUZZY)
	./$(TEST_DIFFCAP)
	./$(TEST_GITPUSH)
	./$(TEST_MSGEDIT)
	./$(TEST_BRACE)
	./$(TEST_AITOOLS)
	./$(TEST_CHATMD)
	./$(TEST_AIINSTR)
	./$(TEST_LINEDIFF)
	./$(TEST_FILETREE)
	./$(TEST_FILEOPS)
	./$(TEST_PLANS)
	./$(TEST_AIWEB)
	./$(TEST_GITSTAT)

ifeq ($(OS),Windows_NT)
EXE=.exe
RM=del
else
EXE=
RM=rm -f
endif

clean:
	$(RM) $(TARGET)$(EXE) $(TEST)$(EXE) $(TEST_SYNTAX)$(EXE) $(TEST_OPTIONS)$(EXE) $(TEST_CLIPCODEC)$(EXE) $(TEST_AI)$(EXE) $(TEST_FUZZY)$(EXE) $(TEST_DIFFCAP)$(EXE) $(TEST_GITPUSH)$(EXE) $(TEST_MSGEDIT)$(EXE) $(TEST_BRACE)$(EXE) $(TEST_AITOOLS)$(EXE) $(TEST_CHATMD)$(EXE) $(TEST_AIINSTR)$(EXE) $(TEST_LINEDIFF)$(EXE) $(TEST_FILETREE)$(EXE) $(TEST_FILEOPS)$(EXE) $(TEST_PLANS)$(EXE) $(TEST_AIWEB)$(EXE) $(TEST_GITSTAT)$(EXE)

.PHONY: clean test
