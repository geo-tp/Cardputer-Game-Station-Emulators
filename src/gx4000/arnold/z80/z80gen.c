typedef char Z80_BYTE;
#if 0
int	Z80_Index_GetOpcodeCountForInstruction(int DisAddr)
{
	unsigned char Opcode;

	/* DD prefix - IX */
	Opcode = Z80_RD_MEM(DisAddr);
	DisAddr++;

	switch (Opcode)
	{
		case 0x0dd:
		case 0x0fd:
		case 0x0ed:
			return 1;

		case 0x0cb:
			return 4;

		default:
		{
			switch (Opcode & 0x0c0)
			{
				case 0x000:
				{
					switch (Opcode & 0x07)
					{
						case 1:
						{
							if ((Opcode & 0x08)!=0)
							{
								/* 00ss1001 - ADD HL,ss */
								return 2;
							}
							else
							{
								/* 00dd0001 - LD dd,nn */
								if (Diss_Index_IsRegIndex((Opcode>>4) & 0x03))
								{
									return 4;
								}
							}
						}
						break;

						case 2:
						{
							/* 00100010 - LD (nnnn),HL */
							/* 00101010 - LD HL,(nn) */

							switch ((Opcode>>4) & 0x03)
							{
								case 2:
									return 4;

								default:
									break;
							}

						}
						break;

						case 3:
						{
							if (Diss_Index_IsRegIndex((Opcode>>4) & 0x03))
								return 2;
						}
						break;

						case 4:
						case 5:
						{
							/* 00rrr100 - INC r */
							/* 00rrr101 - DEC r */
							unsigned char OpcodeIndex;

							OpcodeIndex = ((Opcode>>3) & 0x07);

							if (Diss_Index_IsReg8Bit(OpcodeIndex))
								return Diss_Index_Get8BitRegCount(OpcodeIndex) + 1;
						}
						break;

						case 6:
						{
							/* LD r,n - 00rrr110 */
							unsigned char OpcodeIndex;

							OpcodeIndex = (Opcode>>3) & 0x07;

							if (Diss_Index_IsReg8Bit(OpcodeIndex))
								return Diss_Index_Get8BitRegCount(OpcodeIndex) + 2;
						}
						break;

						default:
							break;
					}
				}
				break;

				case 0x040:
				{
					/* 01xxxxxx */
					/* HALT, LD r,R */

					if (Opcode!=0x076)
					{
						/* 01rrrRRR - LD r,R */

						unsigned char Reg1, Reg2;

						Reg1 = (Opcode>>3) & 0x07;
						Reg2 = Opcode & 0x07;

						if (Diss_Index_IsReg8Bit(Reg1) ||
							Diss_Index_IsReg8Bit(Reg2))
						{
							/* Reg1 or Reg2 is Indexed */
							unsigned char Length1, Length2;

							/* get longest length and return that */
							Length1 = Diss_Index_Get8BitRegCount(Reg1);
							Length2 = Diss_Index_Get8BitRegCount(Reg2);

							if (Length1<Length2)
							{
								return Length2 + 1;
							}

							return Length1 + 1;
						}
					}
				}
				break;

				case 0x080:
				{
					/* 10xxxxxx */
					/* 10000rrr - ADD */
					/* 10001rrr - ADC */
					/* 10010rrr - SUB */
					/* 10011rrr - SBC */
					/* 10100rrr - AND */
					/* 10101rrr - XOR */
					/* 10110rrr - OR */
					/* 10111rrr - CP */

					unsigned char OpcodeIndex;

					OpcodeIndex = (Opcode & 0x07);

					if (Diss_Index_IsReg8Bit(OpcodeIndex))
						return Diss_Index_Get8BitRegCount(OpcodeIndex) + 1;
				}
				break;

				case 0x0c0:
				{
					/* 11xxxxxx */


					switch (Opcode & 0x07)
					{
						case 1:
						{
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0001 - POP qq */
								if (Diss_Index_IsRegIndex((Opcode>>4) & 0x03))
									return 2;
							}
							else
							{
								/* 11001001 - RET */
								/* 11011001 - EXX */
								/* 11101001 - JP (HL) */
								/* 11111001 - LD SP,HL */

								if ((Opcode==0x0f9) || (Opcode==0x0e9))
									return 2;

//								sprintf(OutputString,
//									MiscMneumonics3[((Opcode>>4) & 0x03)]);
							}
						}
						break;

						case 3:
						{
							/* 11001011 - CB prefix */
							if ((Opcode & (3<<4))==(1<<4))
							{
//								unsigned char PortByte;
//
//								PortByte = Z80_RD_MEM(DisAddr);
//
//								/* 11011011 - IN A,(n) */
//								/* 11010011 - OUT (n),A */
//
//								if ((Opcode & (1<<4))!=0)
//								{
//									/* 11011011 - IN A,(n) */
//
//									sprintf(OutputString,"IN A,(#%02x)",
//										PortByte);
//								}
//								else
//								{
//									/* 11010011 - OUT (n),A */
//									sprintf(OutputString,"OUT (#%02x),A",
//										PortByte);
//								}

							}
							else if (Opcode == 0x0c3)
							{
//								/* 11000011 - JP nn */
//								sprintf(OutputString,"JP #%04x",
//									Diss_GetWord(DisAddr));
							}
							else
							{
								/* 11100011 - EX (SP).HL */
								/* 11101011 - EX DE,HL */

								/* 11110011 - DI */
								/* 11111011 - EI */

								if (Opcode==0x0e3)
									return 2;

//								sprintf(OutputString,"%s",
//									MiscMneumonics9[(((Opcode>>3) & 0x07)-4)]);
							}

						}
						break;


						case 5:
						{
							/* 11qq0101 - PUSH qq */
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0101 - PUSH qq */
								if (Diss_Index_IsRegIndex((Opcode>>4) & 0x03))
									return 2;
							}
						}
						break;

						default:
							break;
					}
				}
				break;
			}
		}
		break;
	}

	/* default is 1, which is the Index prefix */
	return 1;
}
#endif
extern int Z80_Index_GetOpcodeCountForInstruction(int DisAddr);

typedef short Z80_WORD;
//typedef char Z80_BYTE;

extern Z80_BYTE Z80_RD_MEM(Z80_WORD Addr);


int	Z80_GetOpcodeCountForInstruction(int Addr)
{
	unsigned char Opcode;
	Z80_WORD		DisAddr = Addr;

	Opcode = Z80_RD_MEM(DisAddr);
	DisAddr++;

	switch (Opcode)
	{
		case 0x0cb:
			/* 00000rrr - RLC */
			/* 00001rrr - RRC */
			/* 00010rrr - RL */
			/* 00011rrr - RR */
			/* 00100rrr - SLA */
			/* 00101rrr - SRA */
			/* 00110rrr - SLL */
			/* 00111rrr - SRL */
			/* 01bbbrrr - BIT */
			/* 10bbbrrr - RES */
			/* 11bbbrrr - SET */
			return 2;

		case 0x0ed:
		{
			/* ED prefix */
			Opcode = Z80_RD_MEM(DisAddr);
			DisAddr++;

			if ((Opcode & 0x0c0)==0x040)
			{
				switch (Opcode & 0x07)
				{
					case 0:
					case 1:
					case 2:
						/* IN r,(C) - 01rrr000 */
						/* OUT (C),r - 01rrr001 */
						/* ADC HL,ss - 01ss1010 */
						/* SBC HL,ss - 01ss0010 */
						return 2;

					case 3:
						/* LD dd,(nn) - 01dd1011 */
						/* LD (nn),dd - 01dd0011 */
						return 4;

					case 4:
					case 5:
					case 6:
					case 7:
						/* NEG - 01xxx100 */
						/* RETI - 01xx1010 */
						/* RETN - 01xx0010 */
						/* IM 0 - 01x00110 */
						/* IM ? - 01x01110 */
						/* IM 1 - 01x10110 */
						/* IM 2 - 01x11110 */
						return 2;
				}

			}
			else if ((Opcode & 0x0e4)==0x0a0)
			{

				switch (Opcode & 0x03)
				{
					case 0:
					case 1:
					case 2:
					case 3:
						/* 10100000 - LDI */
						/* 10101000 - LDD */
						/* 10110000 - LDIR */
						/* 10111000 - LDDR */
						/* 10100001 - CPI */
						/* 10111001 - CPDR */
						/* 10110001 - CPIR */
						/* 10101001 - CPD */
						/* 10100010 - INI */
						/* 10110010 - INIR */
						/* 10101010 - IND */
						/* 10111010 - INDR */
						/* 10100011 - OUTI */
						/* 10110011 - OTIR */
						/* 10101011 - outd */
						/* 10111011 - otdr */
						return 2;
				}
			}
			else
			{
				return 2;
			}
		}
		break;

		case 0x0dd:
		case 0x0fd:
			return Z80_Index_GetOpcodeCountForInstruction(DisAddr);

		default:
		{
			switch (Opcode & 0x0c0)
			{
				case 0x000:
				{
					switch (Opcode & 0x07)
					{
						case 0:
						{
							if ((Opcode & 0x020)!=0)
							{
								/* 001cc000 - JR cc */
								return 2;
							}
							else
							{

								if ((Opcode & 0x010)!=0)
								{
									/* 00010000 - DJNZ */
									/* 00011000 - JR */
									return 2;
								}
								else
								{
									return 1;
								}
							}

						}
						break;

						case 1:
						{
							if ((Opcode & 0x08)!=0)
							{
								/* 00ss1001 - ADD HL,ss */
								return 1;
							}

							/* 00dd0001 - LD dd,nn */
							return 3;
						}

						case 2:
						{
							switch ((Opcode>>4) & 0x03)
							{
								case 0:
								case 1:
									/* 00000010 - LD (BC),A */
									/* 00001010 - LD A,(BC) */
									/* 00010010 - LD (DE),A */
									/* 00011010 - LD A,(DE) */
									return 1;

								case 2:
								case 3:
									/* 00100010 - LD (nnnn),HL */
									/* 00101010 - LD HL,(nn) */
									/* 00110010 - LD (nnnn),A */
									/* 00111010 - LD A,(nnnn) */
									return 3;
							}

						}
						break;

						case 3:
						case 4:
						case 5:
							/* 00ss0011 - INC ss */
							/* 00ss1011 - DEC ss */
							/* 00rrr100 - INC r */
							/* 00rrr101 - DEC r */
							return 1;

						case 6:
							/* LD r,n - 00rrr110 */
							return 2;

						case 7:
							/* 00000111 - RLCA */
							/* 00001111 - RRCA */
							/* 00010111 - RLA */
							/* 00011111 - RRA */
							/* 00100111 - DAA */
							/* 00101111 - CPL */
							/* 00110111 - SCF */
							/* 00111111 - CCF */
							return 1;
					}
				}
				break;

				case 0x040:
					/* 01xxxxxx */
					/* 01110110 - HALT*/
					/* 01rrrRRR - LD r,R */
					return 1;

				case 0x080:
					/* 10xxxxxx */
					/* 10000rrr - ADD */
					/* 10001rrr - ADC */
					/* 10010rrr - SUB */
					/* 10011rrr - SBC */
					/* 10100rrr - AND */
					/* 10101rrr - XOR */
					/* 10110rrr - OR */
					/* 10111rrr - CP */
					return 1;

				case 0x0c0:
				{
					/* 11xxxxxx */

					/* 11110011 - DI */
					/* 11111011 - EI */
					/* 11101011 - EX DE,HL */
					/* 11011011 - IN A,(n) */
					/* 11010011 - OUT (n),A */
					/* 11000011 - JP */
					/* 11100011 - EX (SP).HL */


					switch (Opcode & 0x07)
					{

						case 0:
							/* 11 ccc 000 - RET cc */
							return 1;

						case 1:
						{
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0001 - POP qq */
								return 1;
							}
							else
							{
								/* 11001001 - RET */
								/* 11011001 - EXX */
								/* 11101001 - JP (HL) */
								/* 11111001 - LD SP,HL */

								return 1;
							}
						}
						break;

						case 2:
						case 4:
							/* 11 ccc 010 - JP cc,nnnn */
							/* 11 ccc 100 - CALL cc,nnnn */
							return 3;

						case 3:
						{
							if ((Opcode & (3<<4))==(1<<4))
							{
								/* 11011011 - IN A,(n) */
								/* 11010011 - OUT (n),A */
								return 2;
							}
							else if (Opcode == 0x0c3)
							{
								return 3;
							}
							else
							{
								return 1;
							}
						}
						break;

						case 5:
						{
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0101 - PUSH qq */
								return 1;
							}
							else
							{
								/* 11001101 - CALL nn */
								/* 11011101 - DD */
								/* 11101101 - ED */
								/* 11111101 - FD */
								return 3;
							}
						}
						break;

						case 6:
							/* 11000110 - ADD n */
							/* 11001110 - ADC n */
							/* 11010110 - SUB n */
							/* 11011110 - SBC n */
							/* 10100110 - AND */
							/* 10101110 - XOR */
							/* 10110110 - OR */
							/* 10111110 - CP */
							return 2;

						case 7:
							/* 11ttt111 - RST */
							return 1;
					}

				}
				break;
			}
		}
		break;
	}

	return 0;
}


static int	Z80_Index_GetNopCountForInstruction(Z80_WORD DisAddr,Z80_BYTE Flags)
{
	unsigned char Opcode;

	/* DD prefix - IX */
	Opcode = Z80_RD_MEM(DisAddr);
	DisAddr++;

	switch (Opcode)
	{
		case 0x0dd:
		case 0x0fd:
		case 0x0ed:
		{
			return 1;
		}

		case 0x0cb:
		{
			signed char Offset;

			/* CB prefix */

			/* signed offset from IX */
			Offset = Z80_RD_MEM(DisAddr);
			DisAddr++;

			/* opcode */
			Opcode = Z80_RD_MEM(DisAddr);

			if ((Opcode & 0x0c0)==0x00)
			{
				/* 00000rrr - RLC */
				/* 00001rrr - RRC */
				/* 00010rrr - RL */
				/* 00011rrr - RR */
				/* 00100rrr - SLA */
				/* 00101rrr - SRA */
				/* 00110rrr - SLL */
				/* 00111rrr - SRL */

				return 7;
			}
			else
			{
				/* 01bbbrrr - BIT */
				/* 10bbbrrr - RES */
				/* 11bbbrrr - SET */

				if ((Opcode & 0x0c0)==0x040)
				{
					return 6;
				}

				return 7;
			}
		}
		break;


		default:
		{
			switch (Opcode & 0x0c0)
			{
				case 0x000:
				{
					switch (Opcode & 0x07)
					{
						case 1:
						{
							if ((Opcode & 0x08)!=0)
							{
								/* 00ss1001 - ADD HL,ss */
								return 4;
							}
							else
							{
								/* 00dd0001 - LD dd,nn */
								return 4;
							}

						}
						break;

						case 2:
						{
							switch ((Opcode>>4) & 0x03)
							{
								case 2:
								{
									/* 00100010 - LD (nnnn),HL */
									/* 00101010 - LD HL,(nn) */
									return 6;
								}
								break;

								default:
									break;
							}


						}
						break;

						case 3:
							/* 00ss0011 - INC ss */
							/* 00ss1011 - DEC ss */
							return 3;

						case 4:
						case 5:
						{
							/* 00rrr100 - INC r */
							/* 00rrr101 - DEC r */
							if (((Opcode>>3) & 0x07)==6)
								return 6;

							return 2;

						}

						case 6:
						{
							/* LD r,n - 00rrr110 */
							if (((Opcode>>3) & 0x07)==6)
								return 6;

							return 3;
						}
						break;

						default:
							break;
					}
				}
				break;

				case 0x040:
				{
					/* 01xxxxxx */
					/* HALT, LD r,R */
					unsigned char Reg1,Reg2;
					/* will not get here if defb &dd:HALT is encountered! */
					/* 01rrrRRR - LD r,R */

					Reg1 = (Opcode>>3) & 0x07;
					Reg2 = Opcode & 0x07;

					if ((Reg1==6) || (Reg2==6))
					{
						return 5;
					}

					return 2;
				}
				break;

				case 0x080:
				{
					/* 10xxxxxx */
					/* 10000rrr - ADD */
					/* 10001rrr - ADC */
					/* 10010rrr - SUB */
					/* 10011rrr - SBC */
					/* 10100rrr - AND */
					/* 10101rrr - XOR */
					/* 10110rrr - OR */
					/* 10111rrr - CP */

					if ((Opcode & 0x07)==6)
						return 5;

					return 2;
				}

				case 0x0c0:
				{
					/* 11xxxxxx */


					switch (Opcode & 0x07)
					{
						case 1:
						{
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0001 - POP qq */
								return 4;

							}
							else
							{
								/* 11001001 - RET */
								/* 11011001 - EXX */
								/* 11101001 - JP (HL) */
								/* 11111001 - LD SP,HL */

								if (Opcode==0x0e9)
									return 2;

								if (Opcode==0x0f9)
									return 3;
							}
						}
						break;

						case 3:
						{
							/* 11001011 - CB prefix */
							if ((Opcode & (3<<4))==(1<<4))
							{
								/* 11011011 - IN A,(n) */
								/* 11010011 - OUT (n),A */

							}
							else if (Opcode == 0x0c3)
							{
								/* 11000011 - JP nn */
							}
							else
							{
								/* 11100011 - EX (SP).HL */
								/* 11101011 - EX DE,HL */

								/* 11110011 - DI */
								/* 11111011 - EI */

								if (Opcode==0x0e3)
									return 7;
							}

						}
						break;

						case 5:
						{
							/* 11qq0101 - PUSH qq */
							if ((Opcode & (1<<3))==0)
							{
								return 5;
							}
						}
						break;


						default:
							break;
					}

				}
				break;
			}
		}
		break;
	}

	return -1;
}

/* returns NOP count for whole instruction - values may be different
depending on flags! */
int		Z80_GetNopCountForInstruction(Z80_WORD Addr, Z80_BYTE Flags)
{
	unsigned char Opcode;
	Z80_WORD		DisAddr = Addr;

	Opcode = Z80_RD_MEM(DisAddr);
	DisAddr++;

	switch (Opcode)
	{
		case 0x0cb:
		{
			/* CB prefix */
			Opcode = Z80_RD_MEM(DisAddr);

			if ((Opcode & 0x0c0)==0x00)
			{
				/* 00000rrr - RLC */
				/* 00001rrr - RRC */
				/* 00010rrr - RL */
				/* 00011rrr - RR */
				/* 00100rrr - SLA */
				/* 00101rrr - SRA */
				/* 00110rrr - SLL */
				/* 00111rrr - SRL */

				/* (HL)? */
				if ((Opcode & 0x07)==6)
					return 4;

				return 2;
			}
			else
			{
				/* 01bbbrrr - BIT */
				/* 10bbbrrr - RES */
				/* 11bbbrrr - SET */


				/* (HL)? */
				if ((Opcode & 0x07)==6)
				{
					/* BIT? */
					if ((Opcode & 0x0c0)==0x040)
						return 3;

					/* RES/SET */
					return 4;
				}

				return 2;
			}
		}
		break;

		case 0x0fd:
		case 0x0dd:
			return Z80_Index_GetNopCountForInstruction(DisAddr,Flags);

		case 0x0ed:
		{
			/* ED prefix */
			Opcode = Z80_RD_MEM(DisAddr);
			DisAddr++;

			if ((Opcode & 0x0c0)==0x040)
			{
				switch (Opcode & 0x07)
				{
					case 0:
					case 1:
						/* IN r,(C) - 01rrr000 */
						/* OUT (C),r - 01rrr001 */
						return 4;

					case 2:
						/* ADC HL,ss - 01ss1010 */
						/* SBC HL,ss - 01ss0010 */
						return 4;

					case 3:
						/* LD dd,(nn) - 01dd1011 */
						/* LD (nn),dd - 01dd0011 */
						return 6;

					case 4:
						/* NEG - 01xxx100 */
						return 2;

					case 5:
						/* RETI - 01xx1010 */
						/* RETN - 01xx0010 */
						return 4;

					case 6:
						/* IM 0 - 01x00110 */
						/* IM ? - 01x01110 */
						/* IM 1 - 01x10110 */
						/* IM 2 - 01x11110 */
						return 2;

					case 7:
					{
						if ((Opcode & 0x020)==0)
						{
							/* 01000111 - LD I,A */
							/* 01001111 - LD R,A */
							/* 01010111 - LD A,I */
							/* 01011111 - LD A,R */
							return 3;
						}

						if ((Opcode & 0x010)==0)
						{
							/* 01101111 - RLD */
							/* 01100111 - RRD */
							return 5;
						}

						/* 01110111 - ED NOP */
						/* 01111111 - ED NOP */

						return 2;
					}


				}

			}
			else if ((Opcode & 0x0e4)==0x0a0)
			{

				switch (Opcode & 0x03)
				{
					case 0:
					{
						/* 10100000 - LDI */
						/* 10101000 - LDD */
						/* 10110000 - LDIR */
						/* 10111000 - LDDR */

						// correct?
						if ((Opcode==0x0a0) || (Opcode==0x0a8))
							return 5;

					}
					break;

					case 1:
					{
						/* 10100001 - CPI */
						/* 10111001 - CPDR */
						/* 10110001 - CPIR */
						/* 10101001 - CPD */

						// correct?
						if ((Opcode==0x0a1) || (Opcode==0x0a9))
							return 5;


					}
					break;

					case 2:
					{
						/* 10100010 - INI */
						/* 10110010 - INIR */
						/* 10101010 - IND */
						/* 10111010 - INDR */

						// correct?
						if ((Opcode==0x0a2) || (Opcode==0x0aa))
							return 5;
					}
					break;

					case 3:
					{
						/* 10100011 - OUTI */
						/* 10110011 - OTIR */
						/* 10101011 - outd */
						/* 10111011 - otdr */

						// correct?
						if ((Opcode==0x0a3) || (Opcode==0x0ab))
							return 5;
					}
					break;

				}
			}
			else
				/* ED-NOP */
				return 2;
		}
		break;

		default:
		{
			switch (Opcode & 0x0c0)
			{
				case 0x000:
				{
					switch (Opcode & 0x07)
					{
						case 0:
						{
							if ((Opcode & 0x020)!=0)
							{
								/* 001cc000 - JR cc */


							}
							else
							{
								if ((Opcode & 0x010)!=0)
								{
									/* 00010000 - DJNZ */
									/* 00011000 - JR */

									const char *Instruction;

									if (Opcode==0x010)
									{
										Instruction = "DJNZ";
									}
									else
									{
										return 3;
									}

								}
								else
								{
									/* 00000000 - NOP */
									/* 00001000 - EX AF,AF */
									return 1;
								}

							}
						}
						break;

						case 1:
						{
							if ((Opcode & 0x08)!=0)
							{
								/* 00ss1001 - ADD HL,ss */
								return 3;
							}

							/* 00dd0001 - LD dd,nn */
							return 3;
						}
						break;

						case 2:
						{
							switch ((Opcode>>4) & 0x03)
							{
								case 0:
									/* 00000010 - LD (BC),A */
									/* 00001010 - LD A,(BC) */
									return 2;

								case 1:
									/* 00010010 - LD (DE),A */
									/* 00011010 - LD A,(DE) */
									return 2;

								case 2:
									/* 00100010 - LD (nnnn),HL */
									/* 00101010 - LD HL,(nn) */
									return 5;

								case 3:
									return 4;
							}

						}
						break;

						case 3:
							/* 00ss0011 - INC ss */
							/* 00ss1011 - DEC ss */
							return 2;


						case 4:
						case 5:
							/* 00rrr100 - INC r */
							/* 00rrr101 - DEC r */
							if (((Opcode>>3) & 0x07)==6)
								return 3;

							return 1;

						case 6:
							/* LD r,n - 00rrr110 */
							return 2;

						case 7:
							/* 00000111 - RLCA */
							/* 00001111 - RRCA */
							/* 00010111 - RLA */
							/* 00011111 - RRA */
							/* 00100111 - DAA */
							/* 00101111 - CPL */
							/* 00110111 - SCF */
							/* 00111111 - CCF */
								return 1;
					}
				}
				break;

				case 0x040:
				{
					/* 01xxxxxx */
					/* HALT, LD r,R */

					/* 01rrrRRR - LD r,R */


					if (Opcode == 0x076)
					{
						return 1;
					}
					else
					{
						unsigned char Reg1,Reg2;

						Reg1 = ((Opcode>>3) & 0x07);
						Reg2 = (Opcode & 0x07);

						if ((Reg1==6) || (Reg2==6))
						{
							return 2;
						}

						return 1;
					}
				}
				break;

				case 0x080:
				{
					/* 10xxxxxx */
					/* 10000rrr - ADD */
					/* 10001rrr - ADC */
					/* 10010rrr - SUB */
					/* 10011rrr - SBC */
					/* 10100rrr - AND */
					/* 10101rrr - XOR */
					/* 10110rrr - OR */
					/* 10111rrr - CP */

					if ((Opcode & 0x07)==6)
						return 2;

					return 1;
				}
				break;

				case 0x0c0:
				{
					/* 11xxxxxx */


					switch (Opcode & 0x07)
					{

						case 0:
						{
							/* 11 ccc 000 - RET cc */
#if 0
							sprintf(OutputString,"RET %s",
								ConditionCodes[((Opcode>>3) & 0x07)]);
#endif

						}
						break;

						case 1:
						{
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0001 - POP qq */
								return 3;
							}
							else
							{
								/* 11001001 - RET */
								/* 11011001 - EXX */
								/* 11101001 - JP (HL) */
								/* 11111001 - LD SP,HL */

								switch (Opcode)
								{
									case 0x0c9:
										return 3;

									case 0x0d9:
										return 1;

									case 0x0e9:
										return 1;

									case 0x0f9:
										return 2;

									default:
										break;
								}
							}
						}
						break;

						case 2:
							/* 11 ccc 010 - JP cc,nnnn */
							return 3;

						case 3:
						{
							/* 11001011 - CB prefix */
							if ((Opcode & (3<<4))==(1<<4))
							{
								/* 11011011 - IN A,(n) */
								/* 11010011 - OUT (n),A */
								return 3;
							}
							else if (Opcode == 0x0c3)
							{
								/* 11000011 - JP nn */
								return 3;
							}
							else
							{
								/* 11100011 - EX (SP).HL */
								/* 11101011 - EX DE,HL */

								/* 11110011 - DI */
								/* 11111011 - EI */

								switch (Opcode)
								{
									case 0x0e3:
										return 6;
									case 0x0eb:
										return 1;
									case 0x0f3:
										return 1;
									case 0x0fb:
										return 1;

									default:
										break;
								}

							}

						}
						break;

						case 4:
						{
							/* 11 ccc 100 - CALL cc,nnnn */
#if 0
							sprintf(OutputString,"CALL %s,#%04x",
								ConditionCodes[((Opcode>>3) & 0x07)],
								Diss_GetWord(DisAddr));
#endif
						}
						break;


						case 5:
						{
							/* 11qq0101 - PUSH qq */
							if ((Opcode & (1<<3))==0)
							{
								/* 11qq0101 - PUSH qq */
								return 4;

							}
							else
							{
								/* 11001101 - CALL nn */
								/* 11011101 - DD */
								/* 11101101 - ED */
								/* 11111101 - FD */

								switch (Opcode)
								{
									case 0x0cd:
										return 5;

									default:
										break;
								}
							}
						}
						break;

						case 6:
							/* 11000110 - ADD n */
							/* 11001110 - ADC n */
							/* 11010110 - SUB n */
							/* 11011110 - SBC n */
							/* 10100110 - AND */
							/* 10101110 - XOR */
							/* 10110110 - OR */
							/* 10111110 - CP */
							return 2;

						case 7:
							/* 11ttt111 - RST */
							return 4;
					}

				}
				break;
			}
		}
		break;
	}

	return -1;
}
