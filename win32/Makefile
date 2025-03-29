# Copyright (c) 2024 Roger Brown.
# Licensed under the MIT License.

APPNAME=conlog
SRC=$(APPNAME).c
CL=cl
OBJDIR=obj\$(VSCMD_ARG_TGT_ARCH)
BINDIR=bin\$(VSCMD_ARG_TGT_ARCH)
RESFILE=$(OBJDIR)\$(APPNAME).res
APP=$(BINDIR)\$(APPNAME).exe
MSI=$(APPNAME)-$(DEPVERS_conlog_STR4)-$(VSCMD_ARG_TGT_ARCH).msi

all: $(APP) $(MSI) $(MSIX)
	
clean: 
	if exist $(APP) del $(APP)
	if exist $(OBJDIR)\*.obj del $(OBJDIR)\*.obj
	if exist $(OBJDIR) rmdir $(OBJDIR)
	if exist $(BINDIR) rmdir $(BINDIR)

$(APP): $(SRC) $(OBJDIR) $(BINDIR) $(RESFILE)
	$(CL) 							\
		/Fe$@ 						\
		/Fo$(OBJDIR)\				\
		/W3 						\
		/WX 						\
		/MT 						\
		/I.							\
		/DUNICODE					\
		/DNDEBUG 					\
		/DWIN32_LEAN_AND_MEAN		\
		$(SRC) 						\
		/link						\
		/INCREMENTAL:NO				\
		/PDB:NONE					\
		/SUBSYSTEM:CONSOLE			\
		user32.lib					\
		/VERSION:$(LINKVERSION)		\
		$(RESFILE)					\
		/MANIFEST
	mt -manifest $(APPNAME).manifest $@.manifest -outputresource:$@;#1
	del $@.manifest
	del "$(BINDIR)\$(APPNAME).exp"
	del "$(BINDIR)\$(APPNAME).lib"
	signtool sign /sha1 "$(CertificateThumbprint)" /fd SHA256 /t http://timestamp.digicert.com $@

$(RESFILE): $(APPNAME).rc
	rc /r $(RCFLAGS) "/DDEPVERS_conlog_INT4=$(DEPVERS_conlog_INT4)" "/DDEPVERS_conlog_STR4=\"$(DEPVERS_conlog_STR4)\"" /fo$@ $(APPNAME).rc

$(OBJDIR) $(BINDIR):
	mkdir $@

$(MSI): $(APP) $(CHM)
	"$(WIX)bin\candle.exe" -nologo "conlog.wxs" -dDEPVERS_conlog_STR4=$(DEPVERS_conlog_STR4)
	"$(WIX)bin\light.exe" -nologo -cultures:null -out $@ "conlog.wixobj"
	del "conlog.wixobj"
	del "$(APPNAME)-$(DEPVERS_conlog_STR4)-$(VSCMD_ARG_TGT_ARCH).wixpdb"
	signtool sign /sha1 "$(CertificateThumbprint)" /fd SHA256 /t http://timestamp.digicert.com $@
