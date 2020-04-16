/* SCCSWHAT( "@(#)yypars.c	3.1 88/11/16 22:00:49	" ) */
#line 3 "yypars.c"
# define YYFLAG -1000
# define YYERROR goto yyerrlab
# define YYACCEPT return(0)
# define YYABORT return(1)

#ifdef YYDEBUG				/* RRR - 10/9/85 */
#define yyprintf(a, b, c, d) printf(a, b, c, d)
#else
#define yyprintf(a, b, c, d)
#endif

#ifndef YYPRINT
#define	YYPRINT	printf
#endif

/*	parser for yacc output	*/

#ifdef YYDEBUG
YYSTATIC int yydebug = 0; /* 1 for debugging */
#endif
YYSTATIC YYSTYPE yyv[YYMAXDEPTH];	/* where the values are stored */
YYSTATIC short	yys[YYMAXDEPTH];	/* the parse stack */
YYSTATIC int yychar = -1;			/* current input token number */
YYSTATIC int yynerrs = 0;			/* number of errors */
YYSTATIC short yyerrflag = 0;		/* error recovery flag */

#ifdef YYRECOVER
/*
**  yyscpy : copy f onto t and return a ptr to the null terminator at the
**  end of t.
*/
YYSTATIC	char	*yyscpy(t,f)
	register	char	*t, *f;
	{
	while(*t = *f++)
		t++;
	return(t);	/*  ptr to the null char  */
	}
#endif

#ifndef YYNEAR
#define YYNEAR
#endif
#ifndef YYPASCAL
#define YYPASCAL
#endif
#ifndef YYLOCAL
#define YYLOCAL
#endif
#if ! defined YYPARSER
#define YYPARSER yyparse
#endif
#if ! defined YYLEX
#define YYLEX yylex
#endif

YYLOCAL YYNEAR YYPASCAL YYPARSER()
{
	register	short	yyn;
	short		yystate, *yyps;
	YYSTYPE		*yypv;
	short		yyj, yym;

	yystate = 0;
	yychar = -1;
	yynerrs = 0;
	yyerrflag = 0;
	yyps= &yys[-1];
	yypv= &yyv[-1];

 yystack:    /* put a state and value onto the stack */

	yyprintf( "state %d, char %d\n", yystate, yychar, 0 );
	if( ++yyps > &yys[YYMAXDEPTH] ) {
		yyerror( "yacc stack overflow" );
		return(1);
	}
	*yyps = yystate;
	++yypv;

	*yypv = yyval;

yynewstate:

	yyn = YYPACT[yystate];

	if( yyn <= YYFLAG ) {	/*  simple state, no lookahead  */
		goto yydefault;
	}
	if( yychar < 0 ) {	/*  need a lookahead */
		yychar = YYLEX();
	}
	if( ((yyn += yychar) < 0) || (yyn >= YYLAST) ) {
		goto yydefault;
	}
	if( YYCHK[ yyn = YYACT[ yyn ] ] == yychar ) {		/* valid shift */
		yychar = -1;
		yyval = yylval;
		yystate = yyn;
		if( yyerrflag > 0 ) {
			--yyerrflag;
		}
		goto yystack;
	}

 yydefault:
	/* default state action */

	if( (yyn = YYDEF[yystate]) == -2 ) {
		register	short	*yyxi;

		if( yychar < 0 ) {
			yychar = YYLEX();
		}
/*
**  search exception table, we find a -1 followed by the current state.
**  if we find one, we'll look through terminal,state pairs. if we find
**  a terminal which matches the current one, we have a match.
**  the exception table is when we have a reduce on a terminal.
*/

#if YYOPTTIME
		yyxi = yyexca + yyexcaind[yystate];
		while(( *yyxi != yychar ) && ( *yyxi >= 0 )){
			yyxi += 2;
		}
#else
		for(yyxi = yyexca;
			(*yyxi != (-1)) || (yyxi[1] != yystate);
			yyxi += 2
		) {
			; /* VOID */
			}

		while( *(yyxi += 2) >= 0 ){
			if( *yyxi == yychar ) {
				break;
				}
		}
#endif
		if( (yyn = yyxi[1]) < 0 ) {
			return(0);   /* accept */
			}
		}

	if( yyn == 0 ){ /* error */
		/* error ... attempt to resume parsing */

		switch( yyerrflag ){

		case 0:		/* brand new error */
#ifdef YYRECOVER
			{
			register	int		i,j;

			for(i = 0;
				(yyrecover[i] != -1000) && (yystate > yyrecover[i]);
				i += 3
			) {
				;
			}
			if(yystate == yyrecover[i]) {
				yyprintf("recovered, from state %d to state %d on token %d\n",
						yystate,yyrecover[i+2],yyrecover[i+1]
						);
				j = yyrecover[i + 1];
				if(j < 0) {
				/*
				**  here we have one of the injection set, so we're not quite
				**  sure that the next valid thing will be a shift. so we'll
				**  count it as an error and continue.
				**  actually we're not absolutely sure that the next token
				**  we were supposed to get is the one when j > 0. for example,
				**  for(+) {;} error recovery with yyerrflag always set, stops
				**  after inserting one ; before the +. at the point of the +,
				**  we're pretty sure the guy wants a 'for' loop. without
				**  setting the flag, when we're almost absolutely sure, we'll
				**  give him one, since the only thing we can shift on this
				**  error is after finding an expression followed by a +
				*/
					yyerrflag++;
					j = -j;
					}
				if(yyerrflag <= 1) {	/*  only on first insertion  */
					yyrecerr(yychar,j);	/*  what was, what should be first */
				}
				yyval = yyeval(j);
				yystate = yyrecover[i + 2];
				goto yystack;
				}
			}
#endif
		yyerror("syntax error");

		yyerrlab:
			++yynerrs;

		case 1:
		case 2: /* incompletely recovered error ... try again */

			yyerrflag = 3;

			/* find a state where "error" is a legal shift action */

			while ( yyps >= yys ) {
			   yyn = YYPACT[*yyps] + YYERRCODE;
			   if( yyn>= 0 && yyn < YYLAST && YYCHK[YYACT[yyn]] == YYERRCODE ){
			      yystate = YYACT[yyn];  /* simulate a shift of "error" */
			      goto yystack;
			      }
			   yyn = YYPACT[*yyps];

			   /* the current yyps has no shift onn "error", pop stack */

yyprintf( "error recovery pops state %d, uncovers %d\n", *yyps, yyps[-1], 0 );
			   --yyps;
			   --yypv;
			   }

			/* there is no state on the stack with an error shift ... abort */

	yyabort:
			return(1);


		case 3:  /* no shift yet; clobber input char */

			yyprintf( "error recovery discards char %d\n", yychar, 0, 0 );

			if( yychar == 0 ) goto yyabort; /* don't discard EOF, quit */
			yychar = -1;
			goto yynewstate;   /* try again in the same state */
			}
		}

	/* reduction by production yyn */
yyreduce:
		{
		register	YYSTYPE	*yypvt;
		yyprintf("reduce %d\n",yyn, 0, 0);
		yypvt = yypv;
		yyps -= YYR2[yyn];
		yypv -= YYR2[yyn];
		yyval = yypv[1];
		yym = yyn;
		yyn = YYR1[yyn];		/* consult goto table to find next state */
		yyj = YYPGO[yyn] + *yyps + 1;
		if( (yyj >= YYLAST) || (YYCHK[ yystate = YYACT[yyj] ] != -yyn) ) {
			yystate = YYACT[YYPGO[yyn]];
			}
		switch(yym){
			$A
			}
		}
		goto yystack;  /* stack new state and value */
	}
