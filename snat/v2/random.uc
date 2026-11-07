import { open } from 'fs';

export function random_source() {
    let f = open('/dev/urandom', 'r');
    if (!f) die('kernel randomness unavailable');
    return {
        below: function(n) {
            if (n < 1 || n > 4294967296) die('invalid random bound');
            let limit = 4294967296 - 4294967296 % n, value;
            while (true) {
                let bytes = f.read(4);
                if (length(bytes) != 4) die('short randomness read');
                value = ord(bytes, 0) * 16777216 + ord(bytes, 1) * 65536 + ord(bytes, 2) * 256 + ord(bytes, 3);
                if (value < limit) break;
            }
            return value % n;
        },
        close: function() { f.close(); }
    };
};
