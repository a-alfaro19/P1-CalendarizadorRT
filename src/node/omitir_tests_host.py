# Las pruebas del host (tests/*_tests.c, tests/test_harness.h) viven sueltas
# en la raiz de tests/ y las compila el Makefile. PlatformIO compilaria esos
# archivos junto con cada prueba del nodo (tests/test_*/), asi que se omiten.

import os

Import("env")

RAIZ_TESTS = os.path.normcase(os.path.abspath(env.subst("$PROJECT_TEST_DIR")))


def omitir_raiz_de_tests(nodo):
    carpeta = os.path.normcase(os.path.dirname(nodo.srcnode().get_abspath()))
    return None if carpeta == RAIZ_TESTS else nodo


env.AddBuildMiddleware(omitir_raiz_de_tests)
