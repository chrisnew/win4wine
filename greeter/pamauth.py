"""Check a user's password with PAM, through ctypes (no python3-pam needed).

As a normal user pam_unix checks it with its setuid helper (unix_chkpwd), so
the lock screen needs no privileges. Only the user's own password can be
checked that way."""
import ctypes
from ctypes import CFUNCTYPE, POINTER, Structure, byref, c_char_p, c_int, c_void_p

PAM_SUCCESS, PAM_BUF_ERR, PAM_NEW_AUTHTOK_REQD, PAM_CONV_ERR = 0, 5, 12, 19
PAM_PROMPT_ECHO_OFF = 1


class Message(Structure):
    _fields_ = [('style', c_int), ('text', c_char_p)]


class Response(Structure):
    _fields_ = [('text', c_void_p), ('code', c_int)]


CONVERSE = CFUNCTYPE(c_int, c_int, POINTER(POINTER(Message)), POINTER(POINTER(Response)), c_void_p)


class Conversation(Structure):
    _fields_ = [('converse', CONVERSE), ('data', c_void_p)]


libpam = ctypes.CDLL('libpam.so.0')
libc = ctypes.CDLL(None)
libc.calloc.restype = c_void_p
libc.calloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
libc.strdup.restype = c_void_p
libc.strdup.argtypes = [c_char_p]
libpam.pam_start.argtypes = [c_char_p, c_char_p, POINTER(Conversation), POINTER(c_void_p)]
for function in (libpam.pam_authenticate, libpam.pam_acct_mgmt, libpam.pam_end):
    function.argtypes = [c_void_p, c_int]


def check(service, user, password):
    """Whether PASSWORD is USER's, by the PAM service SERVICE. A wrong one
    takes about two seconds (pam_unix's delay)."""
    secret = password.encode()

    def converse(count, messages, responses, data):
        # PAM frees the array and the strings, so they come from the C library.
        array = libc.calloc(count, ctypes.sizeof(Response))
        if not array:
            return PAM_BUF_ERR
        answers = ctypes.cast(array, POINTER(Response))
        for i in range(count):
            if messages[i].contents.style == PAM_PROMPT_ECHO_OFF:
                answers[i].text = libc.strdup(secret)
                if not answers[i].text:
                    return PAM_CONV_ERR
        responses[0] = answers
        return PAM_SUCCESS

    conversation = Conversation(CONVERSE(converse), None)
    handle = c_void_p()
    status = libpam.pam_start(service.encode(), user.encode(), byref(conversation), byref(handle))
    if status != PAM_SUCCESS:
        return False
    status = libpam.pam_authenticate(handle, 0)
    if status == PAM_SUCCESS:
        status = libpam.pam_acct_mgmt(handle, 0)
        # An expired password must still unlock: it cannot be changed here.
        if status == PAM_NEW_AUTHTOK_REQD:
            status = PAM_SUCCESS
    libpam.pam_end(handle, status)
    return status == PAM_SUCCESS
