#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

# embed files from the "certs" directory as binary data symbols
# in the app
COMPONENT_EMBED_TXTFILES := pushover_root_cert.pem
COMPONENT_EMBED_TXTFILES += cacert.pem
COMPONENT_EMBED_TXTFILES += prvtkey.pem

COMPONENT_ADD_INCLUDEDIRS := .
