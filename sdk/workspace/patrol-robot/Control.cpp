#include <cmath>
#include <algorithm>

#include "Tower.hpp"
#include "Scanner.hpp"
#include "Control.hpp"


static void print_tabs ( FILE * fw, size_t tabs )
{
	while ( tabs-- )
		fputc ( '\t', fw );
}

static void print ( FILE * fw, size_t tabs, DepthObject const & target )
{
	print_tabs ( fw, tabs );
	fprintf ( fw, "{ %d, %d }", target.coordinates.x, target.coordinates.y);
}

TargetList::Targets const & TargetList::targets() const
{
	return _targets;
}

TargetId TargetList::next_id()
{
	TargetId id = _max_id;
	_max_id = (_max_id + 1) % 20;
	return id;
}

double TargetList::distance(DepthObject a, DepthObject b) {
	double xx = a.coordinates.x - b.coordinates.x;
	double yy = a.coordinates.y - b.coordinates.y;
	return sqrt(4*(xx * xx) + (yy * yy)/16.0);
}

TargetId TargetList::insert(DepthObject target)
{
	for (auto& existing_target : _targets) {
		auto dist = distance (target, existing_target.target );
		//fprintf ( bt, "updating: ||new - existing|| == %f\n", dist );
		if (dist < distance_threshold) {
			// Update
			existing_target.target = target;
			existing_target.ttl = TTL(1);
			ev3_speaker_play_tone(tone_updated_target, tone_updated_target_len);
			return existing_target.id;
		}
	}

	_targets.push_back({next_id(), target, TTL(1)});
	ev3_speaker_play_tone(tone_new_target, tone_new_target_len);
	return _targets.back().id;
}

void TargetList::remove_old_targets()
{
	auto begin = std::remove_if(_targets.begin(), _targets.end(),
			[&](const TargetItem& i) {
			return i.ttl == 0;
			});
	_targets.erase(begin, _targets.end());

	std::transform(_targets.begin(), _targets.end(), _targets.begin(),
			[](TargetItem i){
			i.ttl--;
			return i;
			});
}

Control::Control ( ID mutex_id, Tower & tower, IScanner & scanner) :
	_tower ( tower ), _scanner ( scanner ), _locked_id(0xFF)
{
	_mutex_id = mutex_id;
}

void Control::here_is_a_target(DepthObject o)
{
	loc_mtx ( _mutex_id );
	_target_list.insert(std::move(o));
	unl_mtx ( _mutex_id );
	if (_locked_id != 0xFF)
		lock_target(_locked_id);
}

void Control::next_round()
{
	loc_mtx ( _mutex_id );
	_target_list.remove_old_targets();
	unl_mtx ( _mutex_id );
}

static void print ( FILE * fw, TargetItem const & item, SYSTIM now )
{
	fprintf ( fw, "{ ID: %d, target:", item.id);
	print ( fw, 1, item.target );
	fprintf ( fw, "}\n" );
}

static void print ( FILE *fw, TargetList const & tl )
{
	SYSTIM now;
	get_tim ( &now );

	auto const & targets = tl.targets();
	fprintf ( fw, "%u :{\n", targets.size() );
	for ( TargetItem const & item : targets )
	{
		fprintf ( fw, "\t" );
		print   ( fw, item, now );
		fprintf ( fw, "\n" );
	}
	fprintf ( fw, "}\n");
}

static bool is_prefix_of ( const char * prefix, const char * string )
{
	while ( *prefix == *string )
	{
		if ( *prefix == '\0' )
			return true;

		prefix++;
		string++;
	}

	if ( *prefix == '\0' )
		return true;

	return false;
}

static bool read_line(char * buf, size_t bufsz)
{
	while ( true ) {
		char c = fgetc ( bt );

		if ( (c == '\n') || (c == '\r') )
		{
			if (bufsz > 0)
			{
				*buf = '\0';
				return true;
			} else {
				return false;
			}
		}

		if (iscntrl(c))
			return false;

		fputc ( c, bt ); // echo

		if ( bufsz > 0 )
		{
			*buf = c;
			buf++;
			bufsz--;
		}
	}
}

void Control::lock_target ( TargetId id )
{
	loc_mtx ( _mutex_id );

	for ( TargetItem const & it : _target_list.targets() )
	{
		if ( it.id == id )
		{
			unl_mtx ( _mutex_id );
			//fprintf ( bt, "locking at [%d, %d]\n", it.target.coordinates.x,
			//		it.target.coordinates.y );
			_tower.lock_at ( it.target.coordinates );
			return;
		}
	}

	unl_mtx ( _mutex_id );
	return;
}

void Control::usage() const
{
	fprintf ( bt, 	"Commands:\n"
			"\tshoot [rounds]\n"
			"\tcalibrate-tower <angle>\n"
			"\tlist\n"
			"\tlock <target_id>\n"
			"\tlockat <x> <y>\n"
			"\tset-distance <distance>\n"
			"\tunlock\n"

			);
}

void Control::loop()
{
	fprintf ( bt, "Robot started\n" );
	usage();

	while(true)
	{
		fprintf( bt, "> " );
		char buff[80];
		buff[0] = '\0';
		bool valid = read_line (buff, 80 );
		if ( !valid )
		{
			fprintf ( bt, "--discarded\n");
			continue;
		}

		fputc ( '\n', bt );

		if (is_prefix_of("calibrate-tower", buff)) {
			fprintf ( bt, "OK, we will calibrate tower\n" );
			int angle;
			if (1 == sscanf(buff, "calibrate-tower %d", &angle))
				_tower.calibrate(angle);
			else
				usage();
		}
		else if (is_prefix_of("list", buff)) {
			print(bt, _target_list);
		}
		else if (is_prefix_of("lockat", buff)) {
			int x,y;
			if ( 2 == sscanf(buff, "lockat %d %d", &x, &y ) )
				_tower.lock_at ( Coordinates { x, y } );
			else
				usage();

		} else if (is_prefix_of("lock", buff)) {
			unsigned int target_id;
			if (1 == sscanf(buff, "lock %u", &target_id)) {
				lock_target(TargetId(target_id));
				_locked_id = target_id;
			}
			else
				usage();
		} else if (is_prefix_of("unlock", buff)) {
			_locked_id = 0xFF;
			_tower.unlock();
		} 
		else if (is_prefix_of("shoot", buff)) {
			fprintf (bt, "OK, shoot!\n");
			int shots;
			if (1 == sscanf (buff, "shoot %d", &shots))
				_tower.shoot(shots);
			else
				_tower.shoot(1);
		} else if (is_prefix_of("set-background", buff ) )
		{
			int d = 0;
			if ( 1 == sscanf (buff, "set-background %d", &d ) && d >= 0 )
			{
				_scanner.set_background(d);
			} else
				usage();
		} else {
			usage();
		}
	}
}

