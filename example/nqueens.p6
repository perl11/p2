# N-queens, see example/nqueens.pl and example/nqueens.pn
# nqueens N
# 12: 14,200 solutions
my $board_size = +(@*ARGS[0] // 8);
my @occupied;
my @past;
my @solutions;

sub solve($depth) {
  if $depth == $board_size {
    @solutions.push(@past.join(" "));
    return;
  }

  # @diag: marks cells diagonally attackable by any previous queens.
  my @diag;
  for 0 .. @past.elems - 1 -> $i {
    # raku dies on negative indices, perl5 wraps them: skip them
    my $d1 = @past[$i] + $depth - $i;
    my $d2 = @past[$i] - $depth + $i;
    @diag[$d1] = 1 if $d1 < $board_size;
    @diag[$d2] = 1 if $d2 >= 0;
  }

  for 0 .. $board_size - 1 -> $row {
    next if @occupied[$row] || @diag[$row];

    # @past:     row numbers of previous queens
    # @occupied: rows already used. This gets inherited by each
    #            recursion so we don't need to repeatedly look them up
    @past.push($row);
    @occupied[$row] = 1;

    solve($depth + 1);

    # clean up, for next recursion
    @occupied[$row] = 0;
    @past.pop;
  }
}

solve(0);
say "board $board_size: ", @solutions.elems, " solutions";
