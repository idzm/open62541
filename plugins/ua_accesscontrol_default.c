/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 *
 *    Copyright 2016-2017 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2019 (c) HMS Industrial Networks AB (Author: Jonas Green)
 */

#include <open62541/plugin/accesscontrol_default.h>

/* Example access control management. Anonymous and username / password login.
 * The access rights are maximally permissive.
 *
 * FOR PRODUCTION USE, THIS EXAMPLE PLUGIN SHOULD BE REPLACED WITH LESS
 * PERMISSIVE ACCESS CONTROL.
 *
 * For TransferSubscriptions, we check whether the transfer happens between
 * Sessions for the same user. */

typedef struct {
    UA_Boolean allowAnonymous;
    size_t usernamePasswordLoginSize;
    UA_UsernamePasswordLogin *usernamePasswordLogin;
} AccessControlContext;

#define ANONYMOUS_POLICY "open62541-anonymous-policy"
#define CERTIFICATE_POLICY "open62541-certificate-policy"
#define USERNAME_POLICY "open62541-username-policy"
const UA_String anonymous_policy = UA_STRING_STATIC(ANONYMOUS_POLICY);
const UA_String certificate_policy = UA_STRING_STATIC(CERTIFICATE_POLICY);
const UA_String username_policy = UA_STRING_STATIC(USERNAME_POLICY);

/************************/
/* Access Control Logic */
/************************/

static UA_StatusCode
activateSession_default(UA_Server *server, UA_AccessControl *ac,
                        const UA_EndpointDescription *endpointDescription,
                        const UA_ByteString *secureChannelRemoteCertificate,
                        const UA_NodeId *sessionId,
                        const UA_ExtensionObject *userIdentityToken,
                        void **sessionContext) {
    AccessControlContext *context = (AccessControlContext*)ac->context;
    UA_ServerConfig *config = UA_Server_getConfig(server);

    /* The empty token is interpreted as anonymous */
    UA_AnonymousIdentityToken anonToken;
    UA_ExtensionObject tmpIdentity;
    if(userIdentityToken->encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY) {
        UA_AnonymousIdentityToken_init(&anonToken);
        UA_ExtensionObject_init(&tmpIdentity);
        UA_ExtensionObject_setValueNoDelete(&tmpIdentity,
                                            &anonToken,
                                            &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]);
        userIdentityToken = &tmpIdentity;
    }

    /* Could the token be decoded? */
    if(userIdentityToken->encoding < UA_EXTENSIONOBJECT_DECODED)
        return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

    const UA_DataType *tokenType = userIdentityToken->content.decoded.type;
    if(tokenType == &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]) {
        /* Anonymous login */
        if(!context->allowAnonymous)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        const UA_AnonymousIdentityToken *token = (UA_AnonymousIdentityToken*)
            userIdentityToken->content.decoded.data;

        /* Match the beginnig of the PolicyId.
         * Compatibility notice: Siemens OPC Scout v10 provides an empty
         * policyId. This is not compliant. For compatibility, assume that empty
         * policyId == ANONYMOUS_POLICY */
        if(token->policyId.data &&
           (token->policyId.length < anonymous_policy.length ||
            strncmp((const char*)token->policyId.data,
                    (const char*)anonymous_policy.data,
                    anonymous_policy.length) != 0)) {
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
        }
    } else if(tokenType == &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN]) {
        /* Username and password */
        const UA_UserNameIdentityToken *userToken = (UA_UserNameIdentityToken*)
            userIdentityToken->content.decoded.data;

        /* Match the beginnig of the PolicyId */
        if(userToken->policyId.length < username_policy.length ||
           strncmp((const char*)userToken->policyId.data,
                   (const char*)username_policy.data,
                   username_policy.length) != 0) {
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
        }

        /* The userToken has been decrypted by the server before forwarding
         * it to the plugin. This information can be used here. */
        /* if(userToken->encryptionAlgorithm.length > 0) {} */

        /* Empty username and password */
        if(userToken->userName.length == 0 && userToken->password.length == 0)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

        /* Try to match username/pw */
        UA_Boolean match = false;
        for(size_t i = 0; i < context->usernamePasswordLoginSize; i++) {
            if(UA_String_equal(&userToken->userName,
                               &context->usernamePasswordLogin[i].username) &&
               UA_String_equal(&userToken->password,
                               &context->usernamePasswordLogin[i].password)) {
                match = true;
                break;
            }
        }
        if(!match)
            return UA_STATUSCODE_BADUSERACCESSDENIED;
    } else if(tokenType == &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN]) {
        /* x509 certificate */
        const UA_X509IdentityToken *userToken = (UA_X509IdentityToken*)
            userIdentityToken->content.decoded.data;

        /* Match the beginnig of the PolicyId */
        if(userToken->policyId.length < certificate_policy.length ||
           strncmp((const char*)userToken->policyId.data,
                   (const char*)certificate_policy.data,
                   certificate_policy.length) != 0) {
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
        }

        if(!config->sessionPKI.verifyCertificate)
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

       UA_StatusCode res = config->sessionPKI.
            verifyCertificate(&config->sessionPKI, &userToken->certificateData);
        if(res != UA_STATUSCODE_GOOD)
            return UA_STATUSCODE_BADIDENTITYTOKENREJECTED;
    } else {
        /* Unsupported token type */
        return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
    }

    return UA_STATUSCODE_GOOD;
}

static void
closeSession_default(UA_Server *server, UA_AccessControl *ac,
                     const UA_NodeId *sessionId, void *sessionContext) {
}

static UA_UInt32
getUserRightsMask_default(UA_Server *server, UA_AccessControl *ac,
                          const UA_NodeId *sessionId, void *sessionContext,
                          const UA_NodeId *nodeId, void *nodeContext) {
    return 0xFFFFFFFF;
}

static UA_Byte
getUserAccessLevel_default(UA_Server *server, UA_AccessControl *ac,
                           const UA_NodeId *sessionId, void *sessionContext,
                           const UA_NodeId *nodeId, void *nodeContext) {
    return 0xFF;
}

static UA_Boolean
getUserExecutable_default(UA_Server *server, UA_AccessControl *ac,
                          const UA_NodeId *sessionId, void *sessionContext,
                          const UA_NodeId *methodId, void *methodContext) {
    return true;
}

static UA_Boolean
getUserExecutableOnObject_default(UA_Server *server, UA_AccessControl *ac,
                                  const UA_NodeId *sessionId, void *sessionContext,
                                  const UA_NodeId *methodId, void *methodContext,
                                  const UA_NodeId *objectId, void *objectContext) {
    return true;
}

static UA_Boolean
allowAddNode_default(UA_Server *server, UA_AccessControl *ac,
                     const UA_NodeId *sessionId, void *sessionContext,
                     const UA_AddNodesItem *item) {
    return true;
}

static UA_Boolean
allowAddReference_default(UA_Server *server, UA_AccessControl *ac,
                          const UA_NodeId *sessionId, void *sessionContext,
                          const UA_AddReferencesItem *item) {
    return true;
}

static UA_Boolean
allowDeleteNode_default(UA_Server *server, UA_AccessControl *ac,
                        const UA_NodeId *sessionId, void *sessionContext,
                        const UA_DeleteNodesItem *item) {
    return true;
}

static UA_Boolean
allowDeleteReference_default(UA_Server *server, UA_AccessControl *ac,
                             const UA_NodeId *sessionId, void *sessionContext,
                             const UA_DeleteReferencesItem *item) {
    return true;
}

static UA_Boolean
allowBrowseNode_default(UA_Server *server, UA_AccessControl *ac,
                        const UA_NodeId *sessionId, void *sessionContext,
                        const UA_NodeId *nodeId, void *nodeContext) {
    return true;
}

#ifdef UA_ENABLE_SUBSCRIPTIONS
static UA_Boolean
allowTransferSubscription_default(UA_Server *server, UA_AccessControl *ac,
                                  const UA_NodeId *oldSessionId, void *oldSessionContext,
                                  const UA_NodeId *newSessionId, void *newSessionContext) {
    return true;
}
#endif

#ifdef UA_ENABLE_HISTORIZING
static UA_Boolean
allowHistoryUpdateUpdateData_default(UA_Server *server, UA_AccessControl *ac,
                                     const UA_NodeId *sessionId, void *sessionContext,
                                     const UA_NodeId *nodeId,
                                     UA_PerformUpdateType performInsertReplace,
                                     const UA_DataValue *value) {
    return true;
}

static UA_Boolean
allowHistoryUpdateDeleteRawModified_default(UA_Server *server, UA_AccessControl *ac,
                                            const UA_NodeId *sessionId, void *sessionContext,
                                            const UA_NodeId *nodeId,
                                            UA_DateTime startTimestamp,
                                            UA_DateTime endTimestamp,
                                            bool isDeleteModified) {
    return true;
}
#endif

/***************************************/
/* Create Delete Access Control Plugin */
/***************************************/

static void clear_default(UA_AccessControl *ac) {
    UA_Array_delete((void*)(uintptr_t)ac->userTokenPolicies,
                    ac->userTokenPoliciesSize,
                    &UA_TYPES[UA_TYPES_USERTOKENPOLICY]);
    ac->userTokenPolicies = NULL;
    ac->userTokenPoliciesSize = 0;

    AccessControlContext *context = (AccessControlContext*)ac->context;

    if (context) {
        for(size_t i = 0; i < context->usernamePasswordLoginSize; i++) {
            UA_String_clear(&context->usernamePasswordLogin[i].username);
            UA_String_clear(&context->usernamePasswordLogin[i].password);
        }
        if(context->usernamePasswordLoginSize > 0)
            UA_free(context->usernamePasswordLogin);

        UA_free(ac->context);
        ac->context = NULL;
    }
}

UA_StatusCode
UA_AccessControl_default(UA_ServerConfig *config,
                         UA_Boolean allowAnonymous,
                         const UA_ByteString *userTokenPolicyUri,
                         size_t usernamePasswordLoginSize,
                         const UA_UsernamePasswordLogin *usernamePasswordLogin) {
    UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_SERVER,
                   "AccessControl: Unconfigured AccessControl. Users have all permissions.");
    UA_AccessControl *ac = &config->accessControl;

    if(ac->clear)
        clear_default(ac);

    ac->clear = clear_default;
    ac->activateSession = activateSession_default;
    ac->closeSession = closeSession_default;
    ac->getUserRightsMask = getUserRightsMask_default;
    ac->getUserAccessLevel = getUserAccessLevel_default;
    ac->getUserExecutable = getUserExecutable_default;
    ac->getUserExecutableOnObject = getUserExecutableOnObject_default;
    ac->allowAddNode = allowAddNode_default;
    ac->allowAddReference = allowAddReference_default;
    ac->allowBrowseNode = allowBrowseNode_default;

#ifdef UA_ENABLE_SUBSCRIPTIONS
    ac->allowTransferSubscription = allowTransferSubscription_default;
#endif

#ifdef UA_ENABLE_HISTORIZING
    ac->allowHistoryUpdateUpdateData = allowHistoryUpdateUpdateData_default;
    ac->allowHistoryUpdateDeleteRawModified = allowHistoryUpdateDeleteRawModified_default;
#endif

    ac->allowDeleteNode = allowDeleteNode_default;
    ac->allowDeleteReference = allowDeleteReference_default;

    AccessControlContext *context = (AccessControlContext*)
            UA_malloc(sizeof(AccessControlContext));
    if(!context)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    memset(context, 0, sizeof(AccessControlContext));
    ac->context = context;

    /* Allow anonymous? */
    context->allowAnonymous = allowAnonymous;
    if(allowAnonymous) {
        UA_LOG_INFO(&config->logger, UA_LOGCATEGORY_SERVER,
                    "AccessControl: Anonymous login is enabled");
    }

    /* Copy username/password to the access control plugin */
    if(usernamePasswordLoginSize > 0) {
        context->usernamePasswordLogin = (UA_UsernamePasswordLogin*)
            UA_malloc(usernamePasswordLoginSize * sizeof(UA_UsernamePasswordLogin));
        if(!context->usernamePasswordLogin)
            return UA_STATUSCODE_BADOUTOFMEMORY;
        context->usernamePasswordLoginSize = usernamePasswordLoginSize;
        for(size_t i = 0; i < usernamePasswordLoginSize; i++) {
            UA_String_copy(&usernamePasswordLogin[i].username,
                           &context->usernamePasswordLogin[i].username);
            UA_String_copy(&usernamePasswordLogin[i].password,
                           &context->usernamePasswordLogin[i].password);
        }
    }

    /* Set the allowed policies */
    size_t policies = 0;
    if(allowAnonymous)
        policies++;
    if(usernamePasswordLoginSize > 0)
        policies++;
    if(config->sessionPKI.verifyCertificate)
        policies++;
    ac->userTokenPoliciesSize = 0;
    ac->userTokenPolicies = (UA_UserTokenPolicy *)
        UA_Array_new(policies, &UA_TYPES[UA_TYPES_USERTOKENPOLICY]);
    if(!ac->userTokenPolicies)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    ac->userTokenPoliciesSize = policies;

    policies = 0;
    if(allowAnonymous) {
        ac->userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_ANONYMOUS;
        ac->userTokenPolicies[policies].policyId = UA_STRING_ALLOC(ANONYMOUS_POLICY);
        policies++;
    }

    if(config->sessionPKI.verifyCertificate) {
        ac->userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_CERTIFICATE;
        ac->userTokenPolicies[policies].policyId = UA_STRING_ALLOC(CERTIFICATE_POLICY);
#if UA_LOGLEVEL <= 400
        if(UA_ByteString_equal(userTokenPolicyUri, &UA_SECURITY_POLICY_NONE_URI)) {
            UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_SERVER,
                           "x509 Certificate Authentication configured, "
                           "but no encrypting SecurityPolicy. "
                           "This can leak credentials on the network.");
        }
#endif
        UA_ByteString_copy(userTokenPolicyUri,
                           &ac->userTokenPolicies[policies].securityPolicyUri);
        policies++;
    }

    if(usernamePasswordLoginSize > 0) {
        ac->userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_USERNAME;
        ac->userTokenPolicies[policies].policyId = UA_STRING_ALLOC(USERNAME_POLICY);
#if UA_LOGLEVEL <= 400
        if(UA_ByteString_equal(userTokenPolicyUri, &UA_SECURITY_POLICY_NONE_URI)) {
            UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_SERVER,
                           "Username/Password Authentication configured, "
                           "but no encrypting SecurityPolicy. "
                           "This can leak credentials on the network.");
        }
#endif
        UA_ByteString_copy(userTokenPolicyUri,
                           &ac->userTokenPolicies[policies].securityPolicyUri);
    }
    return UA_STATUSCODE_GOOD;
}

