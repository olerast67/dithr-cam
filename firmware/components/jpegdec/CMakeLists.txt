# JPEGDEC (Larry Bank, bitbank2, Apache-2.0), версия 1.8.5.
# Библиотека рассчитана на Arduino, поэтому здесь собирается её C-вариант:
# jpegdec.c просто включает jpeg.inl.
idf_component_register(SRCS "jpegdec.c"
                       INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w)
