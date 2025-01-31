/* -------------------------------------------------------------------------
 *
 * sysattr.h
 *	  openGauss system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sysattr.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef SYSATTR_H
#define SYSATTR_H

/*
 * Attribute numbers for the system-defined attributes
 */
#define SelfItemPointerAttributeNumber (-1)
#define ObjectIdAttributeNumber (-2)
#define MinTransactionIdAttributeNumber (-3)
#define MinCommandIdAttributeNumber (-4)
#define MaxTransactionIdAttributeNumber (-5)
#define MaxCommandIdAttributeNumber (-6)
#define TableOidAttributeNumber (-7)
#define FirstLowInvalidHeapAttributeNumber (-8)
#define K2PgTupleIdAttributeNumber				(-8)
#define K2PgFirstLowInvalidAttributeNumber		(-9)
#ifdef PGXC
#define XC_NodeIdAttributeNumber (-10)
#define BucketIdAttributeNumber (-11)
#endif

/*
 * RowId is an auto-generated K2 record column used for tables without a
 * primary key, but is not present in the postgres table.
 *
 * It is included here to reserve the number and for use in K2PG postgres
 * code that requires knowledge about this column.
 */
#define K2PgRowIdAttributeNumber					(-100)

#define K2PgIdxBaseTupleIdAttributeNumber			(-101)
#define K2PgUniqueIdxKeySuffixAttributeNumber		(-102)
#define K2PgSystemFirstLowInvalidAttributeNumber	(-103)

#endif /* SYSATTR_H */
