#!perl
# the pos()/wantarray flags of the file survive a nested require (they were recomputed for the required file)
BEGIN { open(my $f, ">", "pos-req-tmp.pl") || die; print $f "sub req_helper { return 1 }\n1;\n"; close $f; }
BEGIN { require "./pos-req-tmp.pl"; unlink "pos-req-tmp.pl"; }
my $str = "bird";
$str =~ /i/g;
say pos($str); #=> 2
$str =~ /toolongtomatch/g;
say defined pos($str) ? "def" : "undef"; #=> undef
say req_helper(); #=> 1
$_ = "bird";
m/i/g;
say pos; #=> 2
say pos() == 2 ? "same" : "diff"; #=> same
