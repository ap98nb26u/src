#!/bin/sh
#
# Copyright (C) 2012, 2014, 2016  Internet Systems Consortium, Inc. ("ISC")
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
# OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

SYSTEMTESTTOP=../..
. $SYSTEMTESTTOP/conf.sh

zone=.
infile=root.db.in
zonefile=root.db

cp ../ns2/dsset-example.in dsset-example$TP

keyname=`$KEYGEN -q -r $RANDFILE -a RSASHA1 -b 1024 -n zone $zone`

cat $infile $keyname.key > $zonefile

$SIGNER -P -g -r $RANDFILE -o $zone $zonefile > /dev/null

# Configure the resolving server with a trusted key.
cat $keyname.key | grep -v '^; ' | $PERL -n -e '
local ($dn, $class, $type, $flags, $proto, $alg, @rest) = split;
local $key = join("", @rest);
print <<EOF
trusted-keys {
    "$dn" $flags $proto $alg "$key";
};
EOF
' > trusted.conf

cp trusted.conf ../ns2/trusted.conf
cp trusted.conf ../ns3/trusted.conf

cd ../ns2 && $SHELL -e ./sign.sh
