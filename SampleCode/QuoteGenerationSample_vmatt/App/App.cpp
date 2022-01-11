/*
 * Copyright (C) 2011-2020 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * File: app.cpp
 *
 * Description: Sample application to
 * demonstrate the usage of quote generation.
 */
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#include <Windows.h>
#include <tchar.h>
#endif

#include "sgx_urts.h"
#include "sgx_report.h"
#include "sgx_dcap_ql_wrapper.h"
#include "sgx_pce.h"
#include "sgx_error.h"
#include "sgx_quote_3.h"

#include "Enclave_u.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAXLINE 127
#define PORT 5501

#define SGX_AESM_ADDR "SGX_AESM_ADDR"
#if defined(_MSC_VER)
#define ENCLAVE_PATH _T("enclave.signed.dll")
#else
#define ENCLAVE_PATH "enclave.signed.so"
#endif

bool create_app_enclave_report(sgx_target_info_t qe_target_info, sgx_report_t *app_report)
{
        bool ret = true;
        uint32_t retval = 0;
        sgx_status_t sgx_status = SGX_SUCCESS;
        sgx_enclave_id_t eid = 0;
        int launch_token_updated = 0;
        sgx_launch_token_t launch_token = { 0 };

        sgx_status = sgx_create_enclave(ENCLAVE_PATH,
                SGX_DEBUG_FLAG,
                &launch_token,
                &launch_token_updated,
                &eid,
                NULL);
        if (SGX_SUCCESS != sgx_status) {
                printf("Error, call sgx_create_enclave fail [%s], SGXError:%04x.\n", __FUNCTION__, sgx_status);
                ret = false;
                goto CLEANUP;
        }


        sgx_status = enclave_create_report(eid,
                &retval,
                &qe_target_info,
                app_report);
        if ((SGX_SUCCESS != sgx_status) || (0 != retval)) {
                printf("\nCall to get_app_enclave_report() failed\n");
                ret = false;
                goto CLEANUP;
        }

CLEANUP:
        sgx_destroy_enclave(eid);
        return ret;
}
int main(int argc, char* argv[])
{
    (void)(argc);
    (void)(argv);

    struct sockaddr_in servaddr; // QUOTE

    //struct sockaddr_in servaddr2; // listen signal
    struct sockaddr_in cliaddr, client_addr;
    int listen_sock, accp_sock, nbyte, nbuf;
    int client_sockfd;
    int result;
    int state;
    char msg[10];
    socklen_t addrlen, client_len;

    int s;
    char buf[MAXLINE + 1];
    char filename[20];
    int filesize, fp, filenamesize;
    int sread, total = 0;
    memset(msg, 0x00, 10);
    state = 0;


    int ret = 0;
    quote3_error_t qe3_ret = SGX_QL_SUCCESS;
    uint32_t quote_size = 0;
    uint8_t* p_quote_buffer = NULL;
    sgx_target_info_t qe_target_info;
    sgx_report_t app_report;
    sgx_quote3_t *p_quote;
    sgx_ql_auth_data_t *p_auth_data;
    sgx_ql_ecdsa_sig_data_t *p_sig_data;
    sgx_ql_certification_data_t *p_cert_data;
    FILE *fptr = NULL;
    bool is_out_of_proc = false;
    char *out_of_proc = getenv(SGX_AESM_ADDR);
    if(out_of_proc)
        is_out_of_proc = true;

    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(listen_sock < 0){
        perror("socket fail\n");
        exit(0);
    }

    bzero((char*)&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    int a;
    a = bind(listen_sock, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if(a < 0){
        perror("bind fail\n");
        exit(0);
    }

    listen(listen_sock, 5);

    printf("Waiting attestation input...\n");
    accp_sock = accept(listen_sock, (struct sockaddr*)&cliaddr, &addrlen);

    if(accp_sock < 0){
         perror("accept fail\n");
        exit(0);
    }

    printf("Attestation request\n");
    char ret_msg[] = "Remote Attestation Start\n";
    write(accp_sock, ret_msg, sizeof(ret_msg));

    close(accp_sock);
    close(listen_sock);

#if !defined(_MSC_VER)
    // There 2 modes on Linux: one is in-proc mode, the QE3 and PCE are loaded within the user's process.
    // the other is out-of-proc mode, the QE3 and PCE are managed by a daemon. If you want to use in-proc
    // mode which is the default mode, you only need to install libsgx-dcap-ql. If you want to use the
    // out-of-proc mode, you need to install libsgx-quote-ex as well. This sample is built to demo both 2
    // modes, so you need to install libsgx-quote-ex to enable the out-of-proc mode.
    if(!is_out_of_proc)
    {
        // Following functions are valid in Linux in-proc mode only.
        printf("sgx_qe_set_enclave_load_policy is valid in in-proc mode only and it is optional: the default enclave load policy is persistent: \n");
        printf("set the enclave load policy as persistent:");
        qe3_ret = sgx_qe_set_enclave_load_policy(SGX_QL_PERSISTENT);
        if(SGX_QL_SUCCESS != qe3_ret) {
            printf("Error in set enclave load policy: 0x%04x\n", qe3_ret);
            ret = -1;
            goto CLEANUP;
        }
        printf("succeed!\n");

        // Try to load PCE and QE3 from Ubuntu-like OS system path
        if (SGX_QL_SUCCESS != sgx_ql_set_path(SGX_QL_PCE_PATH, "/usr/lib/x86_64-linux-gnu/libsgx_pce.signed.so") ||
                SGX_QL_SUCCESS != sgx_ql_set_path(SGX_QL_QE3_PATH, "/usr/lib/x86_64-linux-gnu/libsgx_qe3.signed.so")) {

            // Try to load PCE and QE3 from RHEL-like OS system path
            if (SGX_QL_SUCCESS != sgx_ql_set_path(SGX_QL_PCE_PATH, "/usr/lib64/libsgx_pce.signed.so") ||
                SGX_QL_SUCCESS != sgx_ql_set_path(SGX_QL_QE3_PATH, "/usr/lib64/libsgx_qe3.signed.so")) {
                printf("Error in set PCE/QE3 directory.\n");
                ret = -1;
                goto CLEANUP;
            }
        }

        qe3_ret = sgx_ql_set_path(SGX_QL_QPL_PATH, "/usr/lib/x86_64-linux-gnu/libdcap_quoteprov.so.1");
        if (SGX_QL_SUCCESS != qe3_ret) {
            qe3_ret = sgx_ql_set_path(SGX_QL_QPL_PATH, "/usr/lib64/libdcap_quoteprov.so.1");
            if(SGX_QL_SUCCESS != qe3_ret) {
                printf("Error in set QPL directory.\n");
                ret = -1;
                goto CLEANUP;
            }
        }
    }
#endif

    printf("\nStep1: Call sgx_qe_get_target_info:");
    qe3_ret = sgx_qe_get_target_info(&qe_target_info);
    if (SGX_QL_SUCCESS != qe3_ret) {
        printf("Error in sgx_qe_get_target_info. 0x%04x\n", qe3_ret);
                ret = -1;
        goto CLEANUP;
    }
    printf("succeed!");
    printf("\nStep2: Call create_app_report:");
    if(true != create_app_enclave_report(qe_target_info, &app_report)) {
        printf("\nCall to create_app_report() failed\n");
        ret = -1;
        goto CLEANUP;
    }

    printf("succeed!");
    printf("\nStep3: Call sgx_qe_get_quote_size:");
    qe3_ret = sgx_qe_get_quote_size(&quote_size);
    if (SGX_QL_SUCCESS != qe3_ret) {
        printf("Error in sgx_qe_get_quote_size. 0x%04x\n", qe3_ret);
        ret = -1;
        goto CLEANUP;
    }

    printf("succeed!");
    p_quote_buffer = (uint8_t*)malloc(quote_size);
    if (NULL == p_quote_buffer) {
        printf("Couldn't allocate quote_buffer\n");
        ret = -1;
        goto CLEANUP;
    }
    memset(p_quote_buffer, 0, quote_size);

    // Get the Quote
    printf("\nStep4: Call sgx_qe_get_quote:");
    qe3_ret = sgx_qe_get_quote(&app_report,
        quote_size,
        p_quote_buffer);
    if (SGX_QL_SUCCESS != qe3_ret) {
        printf( "Error in sgx_qe_get_quote. 0x%04x\n", qe3_ret);
        ret = -1;
        goto CLEANUP;
    }
    printf("succeed!");

    p_quote = (_sgx_quote3_t*)p_quote_buffer;
    p_sig_data = (sgx_ql_ecdsa_sig_data_t *)p_quote->signature_data;
    p_auth_data = (sgx_ql_auth_data_t*)p_sig_data->auth_certification_data;
    p_cert_data = (sgx_ql_certification_data_t *)((uint8_t *)p_auth_data + sizeof(*p_auth_data) + p_auth_data->size);

    printf("cert_key_type = 0x%x\n", p_cert_data->cert_key_type);

#if _WIN32
    fopen_s(&fptr, "quote.dat", "wb");
#else
    fptr = fopen("quote.dat","wb");
#endif
    if( fptr )
    {
        fwrite(p_quote, quote_size, 1, fptr);
        fclose(fptr);
    }

    if( !is_out_of_proc )
    {
        printf("sgx_qe_cleanup_by_policy is valid in in-proc mode only.\n");
        printf("\n Clean up the enclave load policy:");
        qe3_ret = sgx_qe_cleanup_by_policy();
        if(SGX_QL_SUCCESS != qe3_ret) {
            printf("Error in cleanup enclave load policy: 0x%04x\n", qe3_ret);
            ret = -1;
            goto CLEANUP;
        }
        printf("succeed!\n");
    }

CLEANUP:
    if (NULL != p_quote_buffer) {
        free(p_quote_buffer);
    }
     if(argc != 3){
        printf("usage: %s ip_address port\n", argv[0]);
        exit(0);
    }

    s = socket(PF_INET, SOCK_STREAM, 0);
    if(s < 0){
        perror("socket fail");
        exit(0);
    }

    bzero((char*)&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    servaddr.sin_port = htons(atoi(argv[2]));

    if(ret == -1){
	printf("Error in making Quote\n");
	return -1;
    }


    int connect_res = connect(s, (struct sockaddr *)&servaddr, sizeof(servaddr));

    if(connect_res < 0){
        perror("connect fail\n");
        //exit(0);
	return -1;
    }

    strcpy(filename, "quote.dat");
    printf("Send Quote.dat \n");
    
    fp = open(filename, O_RDONLY);
    if(fp < 0){
        printf("open fail \n");
        //exit(0);
	return -1;
    }

    send(s, filename, sizeof(filename), 0);

    filesize = lseek(fp, 0, SEEK_END);
    send(s, &filesize, sizeof(filesize), 0);
    lseek(fp, 0, SEEK_SET);

    while(total != filesize){
        sread = read(fp, buf, 100);
        total += sread;
        buf[sread] = 0;
        send(s, buf, sread, 0);
        usleep(10000);
    }

    printf("quote send complete\n");
    close(fp);
    close(s);

    return ret;
}
