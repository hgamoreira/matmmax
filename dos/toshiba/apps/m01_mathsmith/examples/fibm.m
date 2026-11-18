function f = fibm(n)
% Fibonacci numbers, by the double recursion (slow on purpose)
if n < 2
  f = n;
else
  f = fibm(n - 1) + fibm(n - 2);
end
end
