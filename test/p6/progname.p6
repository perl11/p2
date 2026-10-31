# $*PROGRAM-NAME: the running script's name (full path, or -e / -E)
say (("$*PROGRAM-NAME").index("progname.p6") >= 0);   #=> True
# assignable: my $*PROGRAM-NAME shadows the dynamic variable in a block
{
  my $*PROGRAM-NAME = "coldfusion";
  say $*PROGRAM-NAME;                                #=> coldfusion
}
