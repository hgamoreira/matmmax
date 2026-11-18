% the primes up to 50, by trial division
tic
n = 50;
count = 0;
for k = 2:n
  isp = 1;
  for d = 2:floor(sqrt(k))
    if mod(k, d) == 0
      isp = 0;
      break
    end
  end
  if isp
    fprintf('%d ', k);
    count = count + 1;
  end
end
fprintf('\n%d primes in %g s\n', count, toc);
