/* reveng.c
 * Greg Cook, 7/Aug/2024
 */

/* CRC RevEng: arbitrary-precision CRC calculator and algorithm finder
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018,
 * 2019, 2020, 2021, 2022, 2024  Gregory Cook
 *
 * This file is part of CRC RevEng.
 *
 * CRC RevEng is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CRC RevEng is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CRC RevEng.  If not, see <https://www.gnu.org/licenses/>.
 */

/* 2021-12-31: modpol() passes only GCD to reveng()
 * 2019-12-07: skip equivalent forms
 * 2019-04-30: brute-force short factor if shortest diff <= 2n
 * 2013-09-16: calini(), calout() work on shortest argument
 * 2013-06-11: added sequence number to uprog() calls
 * 2013-02-08: added polynomial range search
 * 2013-01-18: refactored model checking to pshres(); renamed chkres()
 * 2012-05-24: efficiently build Init contribution string
 * 2012-05-24: removed broken search for crossed-endian algorithms
 * 2012-05-23: rewrote engini() after Ewing; removed modini()
 * 2011-01-17: fixed ANSI C warnings
 * 2011-01-08: fixed calini(), modini() caters for crossed-endian algos
 * 2011-01-04: renamed functions, added calini(), factored pshres();
 *	       rewrote engini() and implemented quick Init search
 * 2011-01-01: reveng() initialises terminating entry, addparms()
 *	       initialises all fields
 * 2010-12-26: renamed CRC RevEng. right results, rejects polys faster
 * 2010-12-24: completed, first tests (unsuccessful)
 * 2010-12-21: completed modulate(), partial sketch of reveng()
 * 2010-12-19: started reveng
 */

/* reveng() can in theory be modified to search for polynomials shorter
 * than the full width as well, but this imposes a heavy time burden on
 * the full width search, which is the primary use case, as well as
 * complicating the search range function introduced in version 1.1.0.
 * It is more effective to search for each shorter width directly.
 */

#include <stdlib.h>

#define FILE void
#include "reveng.h"

static poly_t modpol(const poly_t init, int rflags, int args, const poly_t *argpolys);
static void dispch(const model_t *guess, int *resc, model_t **result, const poly_t divisor, int rflags, int args, const poly_t *argpolys);
static void engini(int *resc, model_t **result, const poly_t divisor, int flags, int args, const poly_t *argpolys);
static void calout(int *resc, model_t **result, const poly_t divisor, const poly_t init, int flags, int args, const poly_t *argpolys);
static void calini(int *resc, model_t **result, const poly_t divisor, int flags, const poly_t xorout, int args, const poly_t *argpolys);
static void chkres(int *resc, model_t **result, const poly_t divisor, const poly_t init, int flags, const poly_t xorout, int args, const poly_t *argpolys);

static const poly_t pzero = PZERO;

model_t *
reveng(const model_t *guess, const poly_t qpoly, int rflags, int args, const poly_t *argpolys) {
	/* Complete the parameters of a model by calculation or brute search. */
	poly_t pwork, rem = PZERO, factor = PZERO, gpoly = PZERO, qqpoly = PZERO;
	model_t *result = NULL, *rptr;
	int resc = 0;
	unsigned long spin = 0, seq = 0;

	if(rflags & R_HAVEP) {
		/* The poly is known.  Engineer, calculate or return
		 * Init and XorOut.
		 */
		dispch(guess, &resc, &result, guess->spoly, rflags, args, argpolys);
	} else {
		/* The poly is not known.
		 * Produce the GCD of all differences between the arguments.
		 */
		if(!plen(guess->spoly))
			goto requit;
		pwork = modpol(guess->init, rflags, args, argpolys);
		/* If too short a difference is returned, there is nothing to do. */
		if(plen(pwork) < plen(guess->spoly) + 1UL)
			goto rpquit;

		/* plen(pwork) >= 2 */
		/* If the shortest difference is the right length for the generator
		 * polynomial (with its top bit), then it *is* the generator polynomial.
		 */
		else if(plen(pwork) == plen(guess->spoly) + 1UL) {
			pcpy(&gpoly, pwork);
			/* Chop the generator. + 1 term is present
			 * as differences come normalized from modpol().
			 */
			pshift(&gpoly,gpoly, 0UL, 1UL, plen(gpoly), 0UL); /* plen(gpoly) >= 1 */
			dispch(guess, &resc, &result, gpoly, rflags, args, argpolys);
			goto rpquit;
		}
		/* Otherwise initialise the trial factor to the starting value. */
		factor = pclone(guess->spoly);
		if(rflags & R_HAVEQ)
			qqpoly = pclone(qpoly);

		/* Truncate trial factor and range end polynomial
		 * if shortest difference is compact.
		 */
		rflags &= ~R_SHORT;
		if(plen(pwork) <= (plen(factor)) << 1) { /* plen(pwork) >= 4, plen(factor) >= 2 */
			rflags |= R_SHORT;
			if((rflags & R_HAVEQ) || ptst(factor)) {
				/* Validate range polynomials.
				 * Ensure proper behaviour if the search space is
				 * naively divided up as per the README.
				 */
				palloc(&rem, plen(pwork) - plen(factor) - 1UL); /* >= 1 */
				pinv(&rem);
				pright(&rem, plen(factor)); /* >= 1 */

				/* If start polynomial out of range, do not search. */
				if(pcmp(&rem, &factor) < 0)
					goto rpquit;
				/* If end polynomial out of range, do not compare,
				 * just quit when trial factor rolls over.
				 */
				else if(pcmp(&rem, &qqpoly) < 0)
					rflags &= ~R_HAVEQ;
				/* Otherwise truncate end polynomial. */
				else if(rflags & R_HAVEQ)
					pright(&qqpoly, plen(pwork) - plen(factor) - 1UL); /* >= 1 */
				pfree(&rem);
			}
			/* Truncate trial factor. */
			pright(&factor, plen(pwork) - plen(factor) - 1UL); /* >= 1 */
		}

		/* Clear the least significant term, to be set in the
		 * loop. qpoly does not need fixing as it is only
		 * compared with odd polys.
		 */
		pshift(&factor, factor, 0UL, 0UL, plen(factor) - 1UL, 1UL);

		/* plen(factor) >= 1 */
		while(piter(&factor) && (~rflags & R_HAVEQ || pcmp(&factor, &qqpoly) < 0)) {
			/* For each possible poly of this size, try
			 * dividing the GCD of the differences.
			 */
			if(!(spin++ & R_SPMASK)) {
				uprog(factor, guess->flags, seq++);
			}
			if(rflags & R_SHORT) {
				/* test whether cofactor divides the GCD */
				rem = pcrc(pwork, factor, pzero, pzero, 0, NULL);
				if(!ptst(rem)) {
					pfree(&rem);
					/* repeat division to get generator polynomial
					 * then test generator against other differences
					 */
					rem = pcrc(pwork, factor, pzero, pzero, 0, &gpoly);
					/* chop generator and ensure + 1 term */
					pshift(&gpoly,gpoly,0UL,1UL,plen(gpoly) - 1UL,1UL);
					piter(&gpoly); /* plen(gpoly) >= 1 */
				}
			} else {
				/* straight divide message by poly, don't multiply by x^n */
				rem = pcrc(pwork, factor, pzero, pzero, 0, 0);
			}
			/* If factor divides all the differences, it is a
			 * candidate.  Search for an Init value for this
			 * poly or if Init is known, log the result.
			 */
			if(!ptst(rem)) {
				/* gpoly || factor is a candidate poly */
				dispch(guess, &resc, &result, (rflags & R_SHORT) ? gpoly : factor, rflags, args, argpolys);
			}
			pfree(&rem);
			if(!piter(&factor))
				break;
		}
		/* Finished with factor and the GCD, free them.
		 */
rpquit:
		pfree(&rem);
		pfree(&qqpoly);
		pfree(&gpoly);
		pfree(&factor);
		pfree(&pwork);
	}

requit:
	if(!(result = realloc(result, ++resc * sizeof(model_t))))
		uerror("cannot reallocate result array");
	rptr = result + resc - 1;
	rptr->spoly  = pzero;
	rptr->init   = pzero;
	rptr->flags  = 0;
	rptr->xorout = pzero;
	rptr->check  = pzero;
	rptr->magic  = pzero;
	rptr->name   = NULL;

	return(result);
}

/* Private functions */

static poly_t
modpol(const poly_t init, int rflags, int args, const poly_t *argpolys) {
	/* Produce the greatest common divisor (GCD) of differences
	 * between pairs of arguments in argpolys[0..args-1].
	 * If R_HAVEI is not set in rflags, only pairs of equal length are
	 * summed.
	 * Otherwise, sums of right-aligned pairs are included, with
	 * the supplied init poly added to the leftmost terms of each
	 * poly of the pair.
	 */
	poly_t gcd = PZERO, work, rem;
	const poly_t *aptr, *bptr, *const eptr = argpolys + args;
	unsigned long alen, blen;
	unsigned int nogcd = 2U;

	if(args < 2) return(gcd);

	for(aptr = argpolys; aptr < eptr; ++aptr) {
		alen = plen(*aptr);
		for(bptr = aptr + 1; bptr < eptr; ++bptr) {
			blen = plen(*bptr);
			if(alen == blen) {
				work = pclone(*aptr);
				psum(&work, *bptr, 0UL);
			} else if(rflags & R_HAVEI && alen < blen) {
				work = pclone(*bptr);
				psum(&work, *aptr, blen - alen);
				psum(&work, init, 0UL);
				psum(&work, init, blen - alen);
			} else if(rflags & R_HAVEI /* && alen > blen */) {
				work = pclone(*aptr);
				psum(&work, *bptr, alen - blen);
				psum(&work, init, 0UL);
				psum(&work, init, alen - blen);
			} else
				work = pzero;

			if(plen(work))
				pnorm(&work);
			if(plen(work)) {
				/* combine work with running gcd */
				if((nogcd >>= 1)) /* true first time only */
					pcpy(&gcd, work);
				else do {
					/* ptst(gcd) != 0 */

					/* optimisation which also accounts
					 * for the way pmod() works.
					 * this emulates one iteration of
					 * a correct loop whereby
					 * (short, long) -> (long, short)
					 * since
					 * poly_mod(short, long) == short
					 * whereas pmod() left-aligns operands
					 */
					if(plen(gcd) < plen(work)) {
						rem = gcd;
						gcd = work;
						work = rem;
					}
					rem = pmod(gcd, work, NULL);
					pfree(&gcd);
					gcd = work;
					work = rem;
					pnorm(&work);
				} while(plen(work));
			}
			pfree(&work);
		}
	}
	return(gcd);
}

static void
dispch(const model_t *guess, int *resc, model_t **result, const poly_t divisor, int rflags, int args, const poly_t *argpolys) {
	if(rflags & R_HAVEI && rflags & R_HAVEX)
		chkres(resc, result, divisor, guess->init, guess->flags, guess->xorout, args, argpolys);
	else if(rflags & R_HAVEI)
		calout(resc, result, divisor, guess->init, guess->flags, args, argpolys);
	else if(rflags & R_HAVEX)
		calini(resc, result, divisor, guess->flags, guess->xorout, args, argpolys);
	else
		engini(resc, result, divisor, guess->flags, args, argpolys);
}

static void
engini(int *resc, model_t **result, const poly_t divisor, int flags, int args, const poly_t *argpolys) {
	/* Search for init values implied by the arguments.
	 * Method from: Ewing, Gregory C. (March 2010).
	 * "Reverse-Engineering a CRC Algorithm". Christchurch:
	 * University of Canterbury.
	 * <http://www.cosc.canterbury.ac.nz/greg.ewing/essays/
	 * CRC-Reverse-Engineering.html>
	 */
	poly_t apoly = PZERO, bpoly, pone = PZERO, *mat, *jptr;
	const poly_t *aptr, *bptr, *iptr;
	unsigned long alen, blen, dlen, ilen, i, j;
	int cy;

	dlen = plen(divisor);

	/* Allocate the CRC matrix */
	if(!(mat = (poly_t *) malloc((dlen << 1) * sizeof(poly_t))))
		uerror("cannot allocate memory for CRC matrix");

	/* Find arguments of the two shortest lengths */
	alen = blen = plen(*(aptr = bptr = iptr = argpolys));
	for(++iptr; iptr < argpolys + args; ++iptr) {
		ilen = plen(*iptr);
		if(ilen < alen) {
			bptr = aptr; blen = alen;
			aptr = iptr; alen = ilen;
		} else if(ilen > alen && (aptr == bptr || ilen < blen)) {
			bptr = iptr; blen = ilen;
		}
	}
	if(aptr == bptr) {
		/* if no arguments are suitable, calculate Init with an
		 * assumed XorOut of 0.  Create a padded XorOut
		 */
		palloc(&apoly, dlen);
		calini(resc, result, divisor, flags, apoly, args, argpolys);
		pfree(&apoly);
		return;
	}

	/* Find the potential contribution of the bottom bit of Init */
	palloc(&pone, 1UL);
	piter(&pone);
	if(blen < (dlen << 1)) {
		palloc(&apoly, dlen); /* >= 1 */
		psum(&apoly, pone, (dlen << 1) - 1UL - blen); /* >= 0 */
		psum(&apoly, pone, (dlen << 1) - 1UL - alen); /* >= 1 */
	} else {
		palloc(&apoly, blen - dlen + 1UL); /* > dlen */
		psum(&apoly, pone, 0UL);
		psum(&apoly, pone, blen - alen); /* >= 1 */
	}
	if(plen(apoly) > dlen) {
		mat[dlen] = pcrc(apoly, divisor, pzero, pzero, 0, 0);
		pfree(&apoly);
	} else {
		mat[dlen] = apoly;
	}

	/* Find the actual contribution of Init */
	apoly = pcrc(*aptr, divisor, pzero, pzero, 0, 0);
	bpoly = pcrc(*bptr, divisor, pzero, apoly, 0, 0);

	/* Populate the matrix */
	palloc(&apoly, 1UL);
	for(jptr=mat; jptr<mat+dlen; ++jptr)
		*jptr = pzero;
	for(iptr = jptr++; jptr < mat + (dlen << 1); iptr = jptr++)
		*jptr = pcrc(apoly, divisor, *iptr, pzero, P_MULXN, 0);
	pfree(&apoly);

	/* Transpose the matrix, augment with the Init contribution
	 * and convert to row echelon form
	 */
	for(i=0UL; i<dlen; ++i) {
		apoly = pzero;
		iptr = mat + (dlen << 1);
		for(j=0UL; j<dlen; ++j)
			ppaste(&apoly, *--iptr, i, j, j + 1UL, dlen + 1UL);
		if(ptst(apoly))
			ppaste(&apoly, bpoly, i, dlen, dlen + 1UL, dlen + 1UL);
		j = pfirst(apoly);
		while(j < dlen && !pident(mat[j], pzero)) {
			psum(&apoly, mat[j], 0UL); /* pfirst(apoly) > j */
			j = pfirst(apoly);
		}
		if(j < dlen)
			mat[j] = apoly; /* pident(mat[j], pzero) || pfirst(mat[j]) == j */
		else
			pfree(&apoly);
	}
	palloc(&bpoly, dlen + 1UL);
	psum(&bpoly, pone, dlen);

	/* Iterate through all solutions */
	do {
		/* Solve the matrix by Gaussian elimination.
		 * The parity of the result, masked by each row, should be even.
		 */
		cy = P_EXHST;
		apoly = pclone(bpoly);
		jptr = mat + dlen;
		for(i=0UL; i<dlen; ++i) {
			/* Compute next bit of Init */
			if(pmpar(apoly, *--jptr))
				psum(&apoly, pone, dlen - 1UL - i);
			/* Toggle each zero row with carry, for next iteration */
			if(cy) {
				if(pident(*jptr, pzero)) {
					/* 0 to 1, no carry */
					*jptr = bpoly;
					cy &= flags;
				} else if(pident(*jptr, bpoly)) {
					/* 1 to 0, carry forward */
					*jptr = pzero;
				}
			}
		}

		/* Trim the augment mask bit */
		praloc(&apoly, dlen);

		/* Test the Init value and add to results if correct */
		calout(resc, result, divisor, apoly, flags, args, argpolys);
		pfree(&apoly);
	} while(!cy);
	pfree(&pone);

	/* Free the matrix. */
	for(jptr=mat; jptr < mat + (dlen << 1); ++jptr)
		if(pident(*jptr, bpoly))
			*jptr = pzero;
		else
			pfree(jptr);
	pfree(&bpoly);
	free(mat);
}

static void
calout(int *resc, model_t **result, const poly_t divisor, const poly_t init, int flags, int args, const poly_t *argpolys) {
	/* Calculate Xorout, check it against all the arguments and
	 * add to results if consistent.
	 */
	poly_t xorout;
	const poly_t *aptr, *iptr;
	unsigned long alen, ilen;

	if(args < 1) return;

	/* find argument of the shortest length */
	alen = plen(*(aptr = iptr = argpolys));
	for(++iptr; iptr < argpolys + args; ++iptr) {
		ilen = plen(*iptr);
		if(ilen < alen) {
			aptr = iptr; alen = ilen;
		}
	}

	xorout = pcrc(*aptr, divisor, init, pzero, 0, 0);
	/* On little-endian algorithms, the calculations yield
	 * the reverse of the actual xorout: in the Williams
	 * model, the refout stage intervenes between init and
	 * xorout.
	 */
	if(flags & P_REFOUT)
		prev(&xorout);

	/* Submit the model to the results table.
	 * Could skip the shortest argument but we wish to check our
	 * calculation.
	 */
	chkres(resc, result, divisor, init, flags, xorout, args, argpolys);
	pfree(&xorout);
}

static void
calini(int *resc, model_t **result, const poly_t divisor, int flags, const poly_t xorout, int args, const poly_t *argpolys) {
	/* Calculate Init, check it against all the arguments and add to
	 * results if consistent.
	 */
	poly_t rcpdiv, rxor, arg, init;
	const poly_t *aptr, *iptr;
	unsigned long alen, ilen;

	if(args < 1) return;

	/* find argument of the shortest length */
	alen = plen(*(aptr = iptr = argpolys));
	for(++iptr; iptr < argpolys + args; ++iptr) {
		ilen = plen(*iptr);
		if(ilen < alen) {
			aptr = iptr; alen = ilen;
		}
	}

	rcpdiv = pclone(divisor);
	prcp(&rcpdiv);
	/* If the algorithm is reflected, an ordinary CRC requires the
	 * model's XorOut to be reversed, as XorOut follows the RefOut
	 * stage.  To reverse the CRC calculation we need rxor to be the
	 * mirror image of the forward XorOut.
	 */
	rxor = pclone(xorout);
	if(~flags & P_REFOUT)
		prev(&rxor);
	arg = pclone(*aptr);
	prev(&arg);

	init = pcrc(arg, rcpdiv, rxor, pzero, 0, 0);
	pfree(&arg);
	pfree(&rxor);
	pfree(&rcpdiv);
	prev(&init);

	/* Submit the model to the results table.
	 * Could skip the shortest argument but we wish to check our
	 * calculation.
	 */
	chkres(resc, result, divisor, init, flags, xorout, args, argpolys);
	pfree(&init);
}

static void
chkres(int *resc, model_t **result, const poly_t divisor, const poly_t init, int flags, const poly_t xorout, int args, const poly_t *argpolys) {
	/* Checks a model against the argument list, and adds to the
	 * external results table if consistent.
	 * Extends the result array and updates the external pointer if
	 * necessary.
	 */
	model_t *rptr;
	poly_t xor, crc;
	const poly_t *aptr = argpolys, *const eptr = argpolys + args;

	/* If the algorithm is reflected, an ordinary CRC requires the
	 * model's XorOut to be reversed, as XorOut follows the RefOut
	 * stage.
	 */
	xor = pclone(xorout);
	if(flags & P_REFOUT)
		prev(&xor);

	for(; aptr < eptr; ++aptr) {
		crc = pcrc(*aptr, divisor, init, xor, 0, 0);
		if(ptst(crc)) {
			pfree(&crc);
			break;
		} else {
			pfree(&crc);
		}
	}
	pfree(&xor);
	if(aptr != eptr) return;

	if(!(*result = realloc(*result, ++*resc * sizeof(model_t))))
		uerror("cannot reallocate result array");

	rptr = *result + *resc - 1;
	rptr->spoly  = pclone(divisor);
	rptr->init   = pclone(init);
	rptr->flags  = flags;
	rptr->xorout = pclone(xorout);
	rptr->check  = pzero;
	rptr->magic  = pzero;
	rptr->name   = NULL;

	/* compute check value for this model */
	mcheck(rptr);

	/* callback to notify new model */
	ufound(rptr);
}
