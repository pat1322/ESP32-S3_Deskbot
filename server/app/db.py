import logging

from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from .config import settings

logger = logging.getLogger("deskbot.db")


def _normalize_url(url: str) -> str:
    # Railway (and Heroku-style providers) hand out "postgres://", but
    # SQLAlchemy 2.x requires the "postgresql://" scheme.
    if url.startswith("postgres://"):
        return "postgresql://" + url[len("postgres://"):]
    return url


_connect_args = {"check_same_thread": False} if settings.database_url.startswith("sqlite") else {}

engine = create_engine(_normalize_url(settings.database_url), connect_args=_connect_args)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)


class Base(DeclarativeBase):
    pass


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def sync_schema() -> None:
    """Create any missing tables, then patch any missing columns onto
    tables that already existed (there's no Alembic here — `create_all`
    alone only ever creates whole new tables, never adds columns to a
    table that's already present in a deployed DB). New columns are
    always added nullable, regardless of the model's declared
    nullability, since existing rows have no value to backfill.
    """
    from . import models  # noqa: F401 — deferred to dodge a models<->db circular
    # import; the side effect of importing it is what registers Job/
    # Settings/Todo onto Base.metadata, so this must run before anything
    # below inspects that metadata. Without it, Base.metadata is empty
    # unless some other already-imported module happened to import
    # `models` first — don't rely on that; make this function self-
    # sufficient regardless of caller/import order.

    Base.metadata.create_all(bind=engine)

    inspector = inspect(engine)
    with engine.begin() as conn:
        for table in Base.metadata.sorted_tables:
            if not inspector.has_table(table.name):
                continue
            existing_cols = {c["name"] for c in inspector.get_columns(table.name)}
            for column in table.columns:
                if column.name in existing_cols:
                    continue
                col_type = column.type.compile(dialect=engine.dialect)
                logger.info("adding missing column %s.%s (%s)", table.name, column.name, col_type)
                conn.execute(text(f"ALTER TABLE {table.name} ADD COLUMN {column.name} {col_type}"))
