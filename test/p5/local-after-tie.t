#!perl
# `local $_` after a block that ties $_ used to crash the parser (the tied name was registered as an element)
{
    package UnderScore;
    sub TIESCALAR { bless \my $self, shift }
    sub FETCH { die "read \$_ forbidden" }
    sub STORE { die "write \$_ forbidden" }
    tie $_, __PACKAGE__;
    untie $_;
}
our $x;
eval { for ("a") { for $x (1, 2) { local $_ = "b"; s/(.*)/+$1/ } } };
print "err=[$@]\n";                          #=> err=[]
$_ = "keep";
{ local $_ = "tmp"; }
print "$_\n";                                #=> keep
