// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_CA_H
#define MHTTP_CA_H

#include <mbedtls/x509_crt.h>

int  mh_ca_init(void);
void mh_ca_free(void);
mbedtls_x509_crt *mh_ca_chain(void);

#endif
