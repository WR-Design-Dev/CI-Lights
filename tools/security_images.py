"""Public Secure Boot v2 image checks. This module never needs private keys."""
import hashlib
import struct
import zlib
from pathlib import Path
from cryptography.exceptions import InvalidSignature

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa, utils


def key_identity(key):
    public = key.public_key() if isinstance(key, rsa.RSAPrivateKey) else key
    if not isinstance(public, rsa.RSAPublicKey) or public.key_size != 3072:
        raise ValueError('Secure Boot requires RSA-3072')
    numbers = public.public_numbers()
    modulus = numbers.n.to_bytes(384, 'little')
    r = pow(2, 6144, numbers.n).to_bytes(384, 'little')
    m = (-pow(numbers.n, -1, 1 << 32)) % (1 << 32)
    hardware = modulus + struct.pack('<I', numbers.e) + r + struct.pack('<I', m)
    spki = public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return {'spki_sha256': hashlib.sha256(spki).hexdigest(),
            'efuse_digest': hashlib.sha256(hardware).hexdigest()}


def verified_signatures(image):
    """Check every occupied block, including CRC, image digest and RSA-PSS."""
    if isinstance(image, (str, Path)):
        image = Path(image).read_bytes()
    if len(image) < 8192 or len(image) % 4096:
        raise ValueError('Signed image must contain a complete aligned signature sector')
    content, sector = image[:-4096], image[-4096:]
    digest = hashlib.sha256(content).digest()
    identities = []
    for index in range(3):
        block = sector[index * 1216:(index + 1) * 1216]
        if block == b'\xff' * 1216:
            continue
        if block[:4] != b'\xe7\x02\x00\x00' or block[4:36] != digest:
            raise ValueError('Invalid Secure Boot signature header or image hash')
        if zlib.crc32(block[:1196]) & 0xffffffff != struct.unpack_from('<I', block, 1196)[0]:
            raise ValueError('Invalid Secure Boot signature CRC')
        public = rsa.RSAPublicNumbers(struct.unpack_from('<I', block, 420)[0],
                                     int.from_bytes(block[36:420], 'little')).public_key()
        if block[1200:] != b'\0' * 16:
            raise ValueError('Invalid signature block padding')
        try:
            public.verify(block[812:1196][::-1], digest,
                          padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                          utils.Prehashed(hashes.SHA256()))
        except InvalidSignature as error:
            raise ValueError('Invalid RSA-PSS image signature') from error
        identity = key_identity(public)
        if hashlib.sha256(block[36:812]).hexdigest() != identity['efuse_digest']:
            raise ValueError('Invalid RSA hardware parameters')
        if identity in identities:
            raise ValueError('Repeated signing key')
        identities.append(identity)
    if not identities or sector[3648:] != b'\xff' * (4096 - 3648):
        raise ValueError('Missing signatures or unexpected signature-sector data')
    return identities


def unsigned_content(image):
    verified_signatures(image)
    return Path(image).read_bytes()[:-4096]
