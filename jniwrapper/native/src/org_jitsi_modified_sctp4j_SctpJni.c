/*
 * Copyright @ 2015 Atlassian Pty Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "org_jitsi_modified_sctp4j_SctpJni.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usrsctp.h>

/* The name of the class which defines the callback methods. */
#define SCTP_CLASSNAME "org/jitsi_modified/sctp4j/SctpJni"

/**
 * Represents the <tt>struct socket</tt> instances initialized by our SCTP
 * integration.
 */
typedef struct _SctpSocket
{
    /** The socket created by the SCTP stack. */
    struct socket *so;
    void *id;
    int localPort;
} SctpSocket;

void
callOnSctpInboundPacket
    (void *socketAddr, void *data, size_t length, uint16_t sid, uint16_t ssn,
        uint16_t tsn, uint32_t ppid, uint16_t context, int flags);

int
callOnSctpOutboundPacket
    (void *socketAddr, void *data, size_t length, uint8_t tos, uint8_t set_df);

int
connectSctp(SctpSocket *sctpSocket, int remotePort);

static void
debugSctpPrintf(const char *format, ...);

static void
infoSctpPrintf(const char *format, ...);

static void
errorSctpPrintf(const char *format, ...);

static void
sctpPError(const char* message);

void
getSctpSockAddr(struct sockaddr_conn *sconn, void *addr, int port);

static int
onSctpInboundPacket
    (struct socket *so, union sctp_sockstore addr, void *data, size_t datalen,
        struct sctp_rcvinfo rcv, int flags, void *ulp_info);

static int
onSctpOutboundPacket
    (void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df);

static int SCTP_EVENT_TYPES[]
    = {
        SCTP_ASSOC_CHANGE,
        SCTP_PEER_ADDR_CHANGE,
        SCTP_SEND_FAILED_EVENT,
        SCTP_SENDER_DRY_EVENT,
        SCTP_STREAM_RESET_EVENT
    };

/** The <tt>jclass</tt> with name <tt>SCTP_CLASSNAME</tt>. */
static jclass Sctp_clazz = 0;
static jmethodID Sctp_receiveCb = 0;
static jmethodID Sctp_sendCb = 0;
static jmethodID Sctp_logDebugCb = 0;
static jmethodID Sctp_logInfoCb = 0;
static jmethodID Sctp_logErrorCb = 0;
/** The global, cached pointer to the Invocation API function table. */
static JavaVM *Sctp_vm = NULL;

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    on_network_in
 * Signature: (J[BII)V
 */
JNIEXPORT void JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_on_1network_1in
    (JNIEnv *env, jclass clazz, jlong ptr, jbyteArray pkt, jint off, jint len)
{
    SctpSocket *sctpSocket;
    jbyte *pkt_;

    sctpSocket = (SctpSocket *) (intptr_t) ptr;

    pkt_ = (*env)->GetByteArrayElements(env, pkt, NULL);
    if (pkt_)
    {
        usrsctp_conninput(
                sctpSocket->id,
                pkt_ + off, len,
                /* ecn_bits */ 0);
        (*env)->ReleaseByteArrayElements(env, pkt, pkt_, JNI_ABORT);
    }
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_accept
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1accept
    (JNIEnv *env, jclass clazz, jlong ptr)
{
    SctpSocket *sctpSocket;
    struct socket* so;

    sctpSocket = (SctpSocket *) (intptr_t) ptr;
    if((so = usrsctp_accept(sctpSocket->so, NULL, NULL)))
    {
        usrsctp_close(sctpSocket->so);
        sctpSocket->so = so;
        return JNI_TRUE;
    }
    else
    {
        return JNI_FALSE;
    }
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_close
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1close
    (JNIEnv *env, jclass clazz, jlong ptr)
{
    SctpSocket *sctpSocket;

    sctpSocket = (SctpSocket *) (intptr_t) ptr;
    usrsctp_close(sctpSocket->so);
    usrsctp_deregister_address(sctpSocket->id);
    free(sctpSocket);
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_connect
 * Signature: (JI)Z
 */
JNIEXPORT jboolean JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1connect
    (JNIEnv *env, jclass clazz, jlong ptr, jint remotePort)
{
    // Try connecting the socket
    return
        connectSctp((SctpSocket *) (intptr_t) ptr, (int) remotePort)
            ? JNI_TRUE
            : JNI_FALSE;
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_finish
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1finish
    (JNIEnv *env, jclass clazz)
{
    return usrsctp_finish() ? JNI_TRUE : JNI_FALSE;
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_init
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1init
    (JNIEnv *env, jclass clazz, jint port)
{
    /*
     * First argument is udp_encapsulation_port which is not relevant to our
     * AF_CONN use of SCTP.
     */
    infoSctpPrintf("org_jitsi_modified_sctp4j_SctpJni.c calling init\n");
    usrsctp_init((uint16_t) port, onSctpOutboundPacket, debugSctpPrintf);

    // Note: this code MUST be called after the call to usrsctp_init, above, as part of that
    // call flow sets the debug to the default level (off)
#ifdef SCTP_DEBUG
    debugSctpPrintf("=====>: org_jitsi_modified_sctp4j_SctpJni.c setting SCTP_DEBUG_ALL\n");
    usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif

    /* TODO(ldixon) Consider turning this on/off. */
    usrsctp_sysctl_set_sctp_ecn_enable(0);

    return JNI_TRUE;
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_listen
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1listen
    (JNIEnv *env, jclass clazz, jlong ptr)
{
    SctpSocket *sctpSocket;
    struct sockaddr_conn sconn;
    struct sockaddr_conn *psconn = &sconn;

    sctpSocket = (SctpSocket *) (intptr_t) ptr;
    /* Bind server socket. */
    getSctpSockAddr(psconn, sctpSocket->id, sctpSocket->localPort);
    if (usrsctp_bind(sctpSocket->so, (struct sockaddr *) psconn, sizeof(sconn))
            < 0)
    {
        sctpPError("usrsctp_bind");
    }
    /* Make server side passive. */
    if (usrsctp_listen(sctpSocket->so, 1) < 0)
        sctpPError("usrsctp_listen");
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_send
 * Signature: (J[BIIZII)I
 */
JNIEXPORT jint JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1send
    (JNIEnv *env, jclass clazz, jlong ptr, jbyteArray data, jint off, jint len,
        jboolean ordered, jint sid, jint ppid)
{
    jbyte *data_;
    ssize_t r;  /* returned by usrsctp_sendv */

    data_ = (*env)->GetByteArrayElements(env, data, NULL);
    if (data_)
    {
        SctpSocket *sctpSocket;
        struct sctp_sndinfo sndinfo;

        sctpSocket = (SctpSocket *) (intptr_t) ptr;

        sndinfo.snd_assoc_id = 0;
        sndinfo.snd_context = 0;
        sndinfo.snd_flags = 0;
        if (JNI_FALSE == ordered)
            sndinfo.snd_flags |= SCTP_UNORDERED;
        sndinfo.snd_ppid = htonl(ppid);
        sndinfo.snd_sid = sid;

        r
            = usrsctp_sendv(
                    sctpSocket->so,
                    data_ + off,
                    len,
                    /* to */ NULL,
                    /* addrcnt */ 0,
                    &sndinfo,
                    (socklen_t) sizeof(sndinfo),
                    SCTP_SENDV_SNDINFO,
                    /* flags */ 0);
        (*env)->ReleaseByteArrayElements(env, data, data_, JNI_ABORT);
    }
    else
    {
        r = -1;
    }
    if (r < 0)
        sctpPError("Sctp send error: ");
    return (jint) r;
}

/*
 * Class:     org_jitsi_modified_sctp4j_SctpJni
 * Method:    usrsctp_socket
 * Signature: (I)J
 */
JNIEXPORT jlong JNICALL
Java_org_jitsi_1modified_sctp4j_SctpJni_usrsctp_1socket
    (JNIEnv *env, jclass clazz, jint localPort, jlong idL)
{
    SctpSocket *sctpSocket;
    struct socket *so;
    struct linger linger_opt;
    struct sctp_assoc_value stream_rst;
    uint32_t nodelay = 1;
    size_t i, eventTypeCount;
    void *id = (void *) (intptr_t) idL;

    struct sctp_event ev;

    sctpSocket = malloc(sizeof(SctpSocket));
    if (sctpSocket == NULL)
    {
        sctpPError("Out of memory!");
        return 0;
    }

    // Register this object's index for usrsctp. This is used by SCTP to
    // direct the packets received (by the created socket) to this class.
    usrsctp_register_address(id);

    so
        = usrsctp_socket(
                AF_CONN,
                SOCK_STREAM,
                IPPROTO_SCTP,
                onSctpInboundPacket,
                /* send_cb */ NULL,
                /* sb_threshold */ 0,
                id);
    if (so == NULL)
    {
        sctpPError("usrsctp_socket");
        free(sctpSocket);
        return 0;
    }

    // Make the socket non-blocking. Connect, close, shutdown etc will not block
    // the thread waiting for the socket operation to complete.
    if (usrsctp_set_non_blocking(so, 1) < 0)
    {
        sctpPError("Failed to set SCTP to non blocking.");
        free(sctpSocket);
        return 0;
    }

    // This ensures that the usrsctp close call deletes the association. This
    // prevents usrsctp from calling onSctpOutboundPacket with references to
    // this class as the address.
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = 0;
    if (usrsctp_setsockopt(so, SOL_SOCKET, SO_LINGER, &linger_opt,
                           sizeof(linger_opt)))
    {
        sctpPError("Failed to set SO_LINGER.");
        free(sctpSocket);
        return 0;
    }

    // Enable stream ID resets.
    stream_rst.assoc_id = SCTP_ALL_ASSOC;
    stream_rst.assoc_value = 1;
    if (usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET,
                           &stream_rst, sizeof(stream_rst)))
    {
        sctpPError("Failed to set SCTP_ENABLE_STREAM_RESET.");
        free(sctpSocket);
        return 0;
    }

    // Nagle.
    if (usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_NODELAY, &nodelay,
                           sizeof(nodelay)))
    {
        sctpPError("Failed to set SCTP_NODELAY.");
        free(sctpSocket);
        return 0;
    }

    // Subscribe to SCTP events.
    eventTypeCount = sizeof(SCTP_EVENT_TYPES) / sizeof(int);
    memset(&ev, 0, sizeof(ev));
    ev.se_assoc_id = SCTP_ALL_ASSOC;
    ev.se_on = 1;
    for (i = 0; i < eventTypeCount; i++)
    {
        ev.se_type = SCTP_EVENT_TYPES[i];
        if (usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_EVENT, &ev, sizeof(ev))
                < 0)
        {
            errorSctpPrintf("Failed to set SCTP_EVENT type: %i\n", ev.se_type);
            free(sctpSocket);
            return 0;
        }
    }

    sctpSocket->so = so;
    sctpSocket->id = id;
    sctpSocket->localPort = (int) localPort;

    return (jlong) (intptr_t) sctpSocket;
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    jint r = JNI_EVERSION;

    if ((*vm)->AttachCurrentThreadAsDaemon(vm, (void **) &env, /* args */ NULL)
            == JNI_OK)
    {
        jclass clazz = (*env)->FindClass(env, SCTP_CLASSNAME);

        if (clazz)
        {
            jmethodID receiveCb
                = (*env)->GetStaticMethodID(
                        env,
                        clazz,
                        "onSctpInboundPacket",
                        "(J[BIIIJII)V");

            if (receiveCb)
            {
                jmethodID sendCb
                    = (*env)->GetStaticMethodID(
                            env,
                            clazz,
                            "onSctpOutboundPacket",
                            "(J[BII)I");

                if (sendCb)
                {
                    jmethodID logDebugCb
                        = (*env)->GetStaticMethodID(
                               env,
                               clazz,
                               "logDebug",
                               "(Ljava/lang/String;)V");

                    jmethodID logInfoCb
                        = (*env)->GetStaticMethodID(
                               env,
                               clazz,
                               "logInfo",
                               "(Ljava/lang/String;)V");

                    jmethodID logErrorCb
                        = (*env)->GetStaticMethodID(
                               env,
                               clazz,
                               "logError",
                               "(Ljava/lang/String;)V");

                    if (logDebugCb && logInfoCb && logErrorCb)
                    {
                        clazz = (*env)->NewGlobalRef(env, clazz);
                        if (clazz)
                        {
                            Sctp_clazz = clazz;
                            Sctp_receiveCb = receiveCb;
                            Sctp_sendCb = sendCb;
                            Sctp_logDebugCb = logDebugCb;
                            Sctp_logInfoCb = logInfoCb;
                            Sctp_logErrorCb = logErrorCb;
                            Sctp_vm = vm;
                            r = JNI_VERSION_1_4;
                        }
                    }
                }
            }
        }
    }
    return r;
}

JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM *vm, void *reserved)
{
    jclass clazz = Sctp_clazz;

    Sctp_clazz = 0;
    Sctp_receiveCb = 0;
    Sctp_sendCb = 0;
    Sctp_logDebugCb = 0;
    Sctp_logInfoCb = 0;
    Sctp_logErrorCb = 0;
    Sctp_vm = NULL;

    if (clazz)
    {
        JNIEnv *env;

        if ((*vm)->AttachCurrentThreadAsDaemon(
                    vm,
                    (void **) &env,
                    /* args */ NULL)
                == JNI_OK)
        {
            (*env)->DeleteGlobalRef(env, clazz);
        }
    }
}

void
callOnSctpInboundPacket
    (void *socketAddr, void *data, size_t length, uint16_t sid, uint16_t ssn,
        uint16_t tsn, uint32_t ppid, uint16_t context, int flags)
{
    JavaVM *vm = Sctp_vm;
    JNIEnv *env;

    if (vm
            && (*vm)->AttachCurrentThreadAsDaemon(
                    vm,
                    (void **) &env,
                    /* args */ NULL)
                == JNI_OK)
    {
        jclass clazz = Sctp_clazz;

        if (clazz)
        {
            jmethodID receiveCb = Sctp_receiveCb;

            if (receiveCb)
            {
                jbyteArray data_ = (*env)->NewByteArray(env, length);

                if (data_)
                {
                    (*env)->SetByteArrayRegion(
                            env,
                            data_,
                            0,
                            length,
                            (jbyte *) data);
                    (*env)->CallStaticVoidMethod(
                            env,
                            clazz,
                            receiveCb,
                            (jlong) (intptr_t) socketAddr,
                            data_,
                            (jint) sid,
                            (jint) ssn,
                            (jint) tsn,
                            (jlong) ntohl(ppid),
                            (jint) context,
                            (jint) flags);
                    /*
                     * XXX It is very important to clear any exception that is
                     * (possibly) currently being thrown. Otherwise, subsequent
                     * JNI invocations may crash the process.
                     */
                    (*env)->ExceptionClear(env);
                    (*env)->DeleteLocalRef(env, data_);
                }
            }
            else
            {
                errorSctpPrintf("Failed to get onSctpInboundPacket method\n");
            }
        }
        else
        {
            errorSctpPrintf("Failed to get SCTP class\n");
        }
    }
    else
    {
        errorSctpPrintf("Failed to attach new thread\n");
    }
}

int
callOnSctpOutboundPacket
    (void *socketAddr, void *data, size_t length, uint8_t tos, uint8_t set_df)
{
    JavaVM *vm = Sctp_vm;
    JNIEnv *env;
    jint r = -1;

    if (vm
            && (*vm)->AttachCurrentThreadAsDaemon(
                    vm,
                    (void **) &env,
                    /* args */ NULL)
                == JNI_OK)
    {
        jclass clazz = Sctp_clazz;

        if (clazz)
        {
            jmethodID sendCb = Sctp_sendCb;

            if (sendCb)
            {
                jbyteArray data_ = (*env)->NewByteArray(env, length);

                if (data_)
                {
                    (*env)->SetByteArrayRegion(
                            env,
                            data_,
                            0,
                            length,
                            (jbyte *) data);
                    r
                        = (*env)->CallStaticIntMethod(
                                env,
                                clazz,
                                sendCb,
                                (jlong) (intptr_t) socketAddr,
                                data_,
                                (jint) tos,
                                (jint) set_df);
                    /*
                     * XXX It is very important to clear any exception that is
                     * (possibly) currently being thrown. Otherwise, subsequent
                     * JNI invocations may crash the process.
                     */
                    (*env)->ExceptionClear(env);
                    (*env)->DeleteLocalRef(env, data_);
                }
            }
            else
            {
                errorSctpPrintf("Failed to get onSctpInboundPacket method\n");
            }
        }
        else
        {
            errorSctpPrintf("Failed to get SCTP class\n");
        }
    }
    else
    {
        errorSctpPrintf("Failed to attach new thread\n");
    }
    return r;
}

int
connectSctp(SctpSocket *sctpSocket, int remotePort)
{
    struct socket *so;
    struct sockaddr_conn sconn;
    struct sockaddr_conn *psconn = &sconn;
    int connect_result;

    so = sctpSocket->so;

    getSctpSockAddr(psconn, sctpSocket->id, sctpSocket->localPort);
    if (usrsctp_bind(so, (struct sockaddr *) psconn, sizeof(sconn)) < 0)
    {
        sctpPError("usrsctp_bind");
        return 0;
    }

    getSctpSockAddr(psconn, sctpSocket->id, remotePort);
    connect_result
        = usrsctp_connect(so, (struct sockaddr *) psconn, sizeof(sconn));
    if (connect_result < 0 && errno != EINPROGRESS)
    {
        sctpPError("usrsctp_connect");
        return 0;
    }

    return 1;
}

static int
logSctpVPrintf(jmethodID logCb, const char* format, va_list args)
{
    char buf[1024];

    JavaVM *vm = Sctp_vm;
    JNIEnv *env;

    vsnprintf(buf, sizeof(buf), format, args);

    if (vm
            && (*vm)->AttachCurrentThreadAsDaemon(
                    vm,
                    (void **) &env,
                    /* args */ NULL)
                == JNI_OK)
    {
        jclass clazz = Sctp_clazz;

        if (clazz && logCb)
        {
            jstring message = (*env)->NewStringUTF(env, buf);

            (*env)->CallStaticVoidMethod(
                 env,
                 clazz,
                 logCb,
                 message);

            (*env)->DeleteLocalRef(env, message);

            return 1;
        }
    }

    return 0;
}

static void
debugSctpPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    logSctpVPrintf(Sctp_logDebugCb, format, args);
    va_end(args);
}

static void
infoSctpPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    logSctpVPrintf(Sctp_logInfoCb, format, args);
    va_end(args);
}

static void
errorSctpPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (!logSctpVPrintf(Sctp_logErrorCb, format, args)) {
        /* Log errors to stdout if JNI doesn't work */
        vprintf(format, args);
    }
    va_end(args);
}

static void
sctpPError(const char* message)
{
    errorSctpPrintf("%s: %s\n", message, strerror(errno));
}

void
getSctpSockAddr(struct sockaddr_conn *sconn, void *addr, int port)
{
    memset(sconn, 0, sizeof(struct sockaddr_conn));
    sconn->sconn_addr = addr;
    sconn->sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
    sconn->sconn_len = sizeof(struct sockaddr_conn);
#endif
    sconn->sconn_port = htons(port);
}

// This is the callback called from usrsctp when data has been received, after
// a packet has been interpreted and parsed by usrsctp and found to contain
// payload data. It is called by a usrsctp thread. It is assumed this function
// will free the memory used by 'data'.
static int
onSctpInboundPacket
    (struct socket *so, union sctp_sockstore addr, void *data, size_t datalen,
        struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
    if (data)
    {
        // Pass the (packet) data to Java.
        if (flags & MSG_NOTIFICATION)
        {
            callOnSctpInboundPacket(
                    ulp_info,
                    data,
                    datalen,
                    /* sid */ 0,
                    /* ssn */ 0,
                    /* tsn */ 0,
                    /* ppid */ 0,
                    /* context */ 0,
                    flags);
        }
        else
        {
            callOnSctpInboundPacket(
                    ulp_info,
                    data,
                    datalen,
                    rcv.rcv_sid,
                    rcv.rcv_ssn,
                    rcv.rcv_tsn,
                    rcv.rcv_ppid,
                    rcv.rcv_context,
                    flags);
        }
        free(data);
    }
    return 1;
}

static int
onSctpOutboundPacket
    (void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df)
{
    if (buffer && length) {
        return callOnSctpOutboundPacket(addr, buffer, length, tos, set_df);
    }

    /* FIXME not sure about this value, but an error for now */
    return -1;
}
