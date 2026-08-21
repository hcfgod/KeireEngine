package JSON;

use strict;
use warnings;
use Exporter 'import';
use JSON::PP ();

our @EXPORT = qw(decode_json);

sub decode_json
{
    return JSON::PP::decode_json($_[0]);
}

1;
